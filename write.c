/*
 * NIBTOOL write routines
 * Copyright 2005-2011 C64 Preservation Project
 * based on MNIB by Markus Brenner <markus(at)brenner(dot)de>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"

void
master_track(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, int track, size_t tracklen)
{
	int i,leader;
	static BYTE last_density = -1;
	BYTE rawtrack[NIB_TRACK_LENGTH*2];

	if(track_inc==1) leader=0;
	else leader=10;

	if(track_density[track] & BM_NO_SYNC)
		memset(rawtrack, 0x55, sizeof(rawtrack));
	else
		memset(rawtrack, fillbyte, sizeof(rawtrack));

	/* merge track data */
	memcpy(rawtrack + leader, track_buffer + (track * NIB_TRACK_LENGTH), tracklen);

	/* check for and correct initial too short sync mark */
	if( ((!(track_density[track] & BM_NO_SYNC)) &&
	    (track_buffer[track * NIB_TRACK_LENGTH] == 0xff) &&
	    (track_buffer[(track * NIB_TRACK_LENGTH) + 1] != 0xff)) || (presync) )
	{
		if(presync>=leader) presync=leader-2;
		if(verbose) printf("[presync:%d]",presync);
		memset(rawtrack + leader - presync, 0xff, presync+1); // Overwrites first sync byte just in case it's not 0xFF
	}

	/* handle short tracks */
	if(tracklen < capacity[track_density[track]&3])
	{
			if(verbose) printf("[pad:%d]", capacity[track_density[track]&3] - tracklen);
			tracklen = capacity[track_density[track]&3];
	}

	/* "fix" for track 18 mastering */
	if(track==18*2)
		memcpy(rawtrack + leader + tracklen - 5, "UJMSU", 5);

	/* replace 0x00 bytes by 0x01, as 0x00 indicates end of track */
	if(!use_floppycode_srq)  // not in srq code
		replace_bytes(rawtrack, sizeof(rawtrack), 0x00, 0x01);

	/* step to destination track and set density */
	if((fattrack)&&(track==fattrack+2))
		step_to_halftrack(fd, track+1);
	else
		step_to_halftrack(fd, track);

	if((fattrack)&&((track==fattrack)||(track==fattrack+2)))
			printf("[fat track]");

	if((track_density[track]&3) != last_density)
	{
		set_density(fd, track_density[track]&3);
		if(verbose>1) printf("[D]");
		last_density = track_density[track]&3;
	}

	// try to do track alignment through simple timers
	if((skew||align_disk) && (auto_capacity_adjust))
	{
		/* subtract overhead from one revolution;
	    adjust for motor speed and density; */
		align_delay = (int)((motor_speed*200000)/300)-18000; // roughly the step time is 18
		align_delay += skew*1000;
		if(align_delay>200000) align_delay-=200000;
		printf("[skew:%d][delay:%d]", skew, align_delay);
	    msleep(align_delay);
    }

	/* burst send track */
	for (i = 0; i < 3; i ++)
	{
		send_mnib_cmd(fd, FL_WRITE, NULL, 0);

		/* Neither of these currently work with SRQ */
		/* IHS will lock forever if IHS is set and it sees no index hole, i.e. side 2 of flippy disk or there is no compatible IHS */
		/* Arnd has some code to test for it, not implemented yet */
		burst_write(fd, (unsigned char)((ihs) ? 0x00 : 0x03));

		/* align disk waits until end of sync before writing */
		//burst_write(fd, (unsigned char)((align_disk) ? 0xfb : 0x00));
		burst_write(fd, (unsigned char)(0x00));

		if (burst_write_track(fd, rawtrack, (int)(tracklen + leader + 1)))
			break;
		else
		{
			//putchar('?');
			printf("(timeout) ");
			fflush(stdin);
			burst_read(fd);
			//msleep(500);
			//printf("%c ", test_par_port(fd)? '+' : '-');
			test_par_port(fd);
		}
	}

	if(i == 3)
	{
		printf("\n\nNo good write of track due to timeouts.  Aborting!\n");
		exit(1);
	}
}

void
master_disk(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track, verified, retries, added_sync=0, addsyncloops;
	size_t badgcr, length, verlen, verlen2;
	BYTE verbuf1[NIB_TRACK_LENGTH], verbuf2[NIB_TRACK_LENGTH], verbuf3[NIB_TRACK_LENGTH], align;
	size_t gcr_diff;
	char errorstring[0x1000];

	//if(track_inc==1) unformat_disk(fd);

	for (track=backwards?end_track:start_track; backwards?(track>=start_track):(track<=end_track); backwards?(track-=track_inc):(track+=track_inc))
	{
		/* double-check our sync-flag assumptions and process track for remaster */
		track_density[track] =
			check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		/* engineer killer track */
		if(track_density[track] & BM_FF_TRACK)
		{
				fill_track(fd, track, 0xFF);
				if(verbose) printf("\n%4.1f: KILLED!",  (float) track / 2);
				continue;
		}

		/* zero out empty tracks entirely */
		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]))
		{
				if(track_inc!=1)
				{
					fill_track(fd, track, 0x00);
					if(verbose) printf("\n%4.1f: UNFORMATTED!",  (float) track / 2);
				}
				continue;
		}

		/* user display */
		if(verbose)
		{
			printf("\n%4.1f: (", (float)track/2);
			printf("%d", track_density[track]&3);
			if ((track_density[track]&3) != speed_map[track/2]) printf("!");
			printf(":%d) ", track_length[track]);
			if (track_density[track] & BM_NO_SYNC) printf("NOSYNC ");
			if (track_density[track] & BM_FF_TRACK) printf("KILLER ");
			printf("WRITE ");
		}

		/* loop last byte of track data for filler
		   we do this before processing track in case we get wrong byte */
		fillbyte = track_buffer[(track * NIB_TRACK_LENGTH) + track_length[track] - 1];
		if(verbose) printf("[fill:$%.2x]", fillbyte);

		if((increase_sync)&&(track_length[track])&&(!(track_density[track]&BM_NO_SYNC))&&(!(track_density[track]&BM_FF_TRACK)))
		{
			for(addsyncloops=0;addsyncloops<increase_sync;addsyncloops++)
			{
				added_sync = lengthen_sync(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track], capacity[track_density[track]&3]);
				track_length[track] += added_sync;
				if(verbose) printf("[+sync:%d]", added_sync);
			}
		}

		badgcr = check_bad_gcr(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]);
		if(verbose) printf("[weak:%d]", badgcr);

		length = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH),
			track_density[track], track_length[track]);

		master_track(fd, track_buffer, track_density, track, length);

		if(track_match)	// Try to verify our write
		{
			verified=retries=0;
			while(!verified)
			{
				// Don't bother to compare unformatted or bad data
				if (track_length[track] == NIB_TRACK_LENGTH) break;

				memset(verbuf1, 0, NIB_TRACK_LENGTH);
				if((ihs) && (!(track_density[track] & BM_NO_SYNC)))
					send_mnib_cmd(fd, FL_READIHS, NULL, 0);
				else if (Use_SCPlus_IHS) // "-j"
					send_mnib_cmd(fd, FL_IHS_READ_SCP, NULL, 0);
				else
				{
					if ((track_density[track] & BM_NO_SYNC) || (track_density[track] & BM_FF_TRACK))
						send_mnib_cmd(fd, FL_READWOSYNC, NULL, 0);
					else
						send_mnib_cmd(fd, FL_READNORMAL, NULL, 0);
				}
				burst_read(fd);
				burst_read_track(fd, verbuf1, NIB_TRACK_LENGTH);

				memset(verbuf2, 0, NIB_TRACK_LENGTH);
				memset(verbuf3, 0, NIB_TRACK_LENGTH);
				verlen   = extract_GCR_track(verbuf2, verbuf1, &align, track/2, track_length[track], track_length[track]);
				verlen2 = extract_GCR_track(verbuf3, track_buffer+(track * NIB_TRACK_LENGTH), &align, track/2, track_length[track], track_length[track]);

				if(verbose) printf("\n      (%d:%d) VERIF", track_density[track]&3, verlen);
				fprintf(fplog, "\n      (%d:%d) VERIF", track_density[track]&3, verlen);

				// Fix bad GCR in tracks for compare
				badgcr = check_bad_gcr(verbuf2, track_length[track]);
				if(verbose>1) printf("(badgcr=%.4d:", badgcr);
				badgcr = check_bad_gcr(verbuf3, track_length[track]);
				if(verbose>1) printf("%.4d)", badgcr);

				// compare raw gcr data
				gcr_diff = compare_tracks(verbuf3, verbuf2, verlen, verlen, 1, errorstring);
				if(verbose) printf(" (diff:%.4d) ", (int)gcr_diff);
				fprintf(fplog, " (diff:%.4d) ", (int)gcr_diff);


				if(gcr_diff <= (size_t)sector_map[track/2]+10)
				{
					printf("OK ");
					verified=1;
				}
				else if(gcr_diff <= badgcr)
				{
					printf("WEAK OK");
					verified=1;
				}
				else
				{
					retries++;
					printf("Retry %d ", retries);
					fill_track(fd, track, 0x00);
					master_track(fd, track_buffer, track_density, track, length);
				}
				if(((track>70)&&(retries>=3))||(retries>=10))
				{
					printf("\n      Write verify FAILED - Odd data or bad media! ");
					verified=1;
				}
			}
		}
	}
}

void
master_disk_raw(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track, density;
	BYTE trackbuf[NIB_TRACK_LENGTH];
	char testfilename[16];
	FILE *trkin = '\0';
	size_t length;

	for (track=backwards?end_track:start_track; backwards?(track>=start_track):(track<=end_track); backwards?(track-=track_inc):(track+=track_inc))
	{
		printf("\n%4.1f:", (float) track / 2);

		// read in raw track at density (in filename)
		for (density = 3; density >= 0; density--)
		{
			sprintf(testfilename, "raw/tr%.1fd%d", (float) track/2, density);

			if( (trkin = fopen(testfilename, "rb")) )
			{
				if(verbose) printf(" [%s] ", testfilename);
				break;
			}
		}

		if (trkin)
		{
			/* erase mem and grab data from file */
			memset(trackbuf, 0x00, sizeof(trackbuf));
			fseek(trkin, 0, SEEK_END);
			length = ftell(trkin);
			rewind(trkin);
			fread(trackbuf, length, 1, trkin); // @@@SRT: check success
			fclose(trkin);

			if(length == 0)
				length = NIB_TRACK_LENGTH;

			/* process track */
			memcpy(track_buffer + (track * NIB_TRACK_LENGTH), trackbuf, NIB_TRACK_LENGTH);
			track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), density, length);
			//length = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], length);

			printf(" (%d", track_density[track] & 3);
			if ( (track_density[track]&3) != speed_map[track/2])
				printf("!=%d", speed_map[track/2]);
			if (track_density[track] & BM_NO_SYNC)
					printf(":NOSYNC");
			else if (track_density[track] & BM_FF_TRACK)
				printf(":KILLER");
			printf(") (%d) ", length);

			/* truncate the end if needed (reduce tail) */
			if (length > capacity[density & 3])
			{
				printf(" (trunc:%d) ",  length - capacity[density & 3]);
				length = capacity[density & 3];
			}
			master_track(fd, track_buffer, track_density, track, length);
		}
		else
			printf(" [missing track file - skipped]");
	}
}

void
unformat_disk(CBM_FILE fd)
{
	/* this routine can write all 1's and all 0's alternatively to try to both
		fix old media into working again, and wiping all data */
	int track, i;

	motor_on(fd);
	set_density(fd, 2);

	printf("Wiping/Unformatting...");

	for (track = start_track; track <= end_track; track += 1/*track_inc*/)
	{
		if(verbose>1) printf("\n%4.1f:",  (float) track/2);
		for(i=0;i<unformat_passes; i++)
		{
			if(verbose>1) printf(".");
			//if(read_killer) fill_track(fd, track, 0xFF);
			fill_track(fd, track, 0x00);
		}
		if(verbose>1) printf("UNFORMATTED!");
	}
}

void fill_track(CBM_FILE fd, int track, BYTE fill)
{
	// step head
	step_to_halftrack(fd, track);

	// write all $ff bytes
	send_mnib_cmd(fd, FL_FILLTRACK, NULL, 0);
	burst_write(fd, fill);  // 0xff byte is all sync "killer" track
	burst_read(fd);
}


void speed_adjust(CBM_FILE fd)
{
	int i, cap;

	printf("\nTesting drive motor speed for 100 loops.\n");
	printf("--------------------------------------------------\n");
	printf("Track 41.5 will be destroyed!\n");

	motor_on(fd);
	step_to_halftrack(fd, 83);
	set_bitrate(fd, 2);

	for (i=0; i<100; i++)
	{
		cap = track_capacity(fd);
		printf("Speed = %.2frpm\n", DENSITY2 / cap);
	}

}

/* This routine measures track capacity at all densities */
void adjust_target(CBM_FILE fd)
{
	int i=3, j=0;
	int cap[DENSITY_SAMPLES];
	int cap_high[4], cap_low[4], cap_margin[4];
	int run_total;
	int capacity_margin = 0;
	BYTE track_dens[4] = { 32*2, 27*2, 21*2, 10*2 };

	//printf("\nTesting track capacity at each density\n");
	//printf("--------------------------------------------------\n");
	printf("\nTesting track capacity/motor speed\n");

	for (i = 0; i <= 3; i++)
	{
		cap_high[i] = 0;
		cap_low[i] = 0xffff;

		if( (start_track < track_dens[i]) && (end_track > track_dens[i]))
			step_to_halftrack(fd, track_dens[i]);
		else
			step_to_halftrack(fd, start_track);

		set_bitrate(fd, (BYTE)i);

		if(verbose) printf("%d: ", i);

		for(j = 0, run_total = 0; j < DENSITY_SAMPLES; j++)
		{
			cap[j] = track_capacity(fd);
			if(verbose) printf("%d ", cap[j]);
			run_total += cap[j];
			if(cap[j] > cap_high[i]) cap_high[i] = cap[j];
			if(cap[j] < cap_low[i]) cap_low[i] = cap[j];
		}
		capacity[i] = run_total / DENSITY_SAMPLES ;
		cap_margin[i] = cap_high[i] - cap_low[i];

		if(cap_margin[i] > capacity_margin)
			capacity_margin = cap_margin[i];

		switch(i)
		{
			case 0:
				if(verbose) printf("(%.2frpm) margin:%d\n", DENSITY0 / capacity[0], cap_margin[i]);
				break;

			case 1:
				if(verbose) printf("(%.2frpm) margin:%d\n", DENSITY1 / capacity[1], cap_margin[i]);
				break;

			case 2:
				if(verbose) printf("(%.2frpm) margin:%d\n", DENSITY2 / capacity[2], cap_margin[i]);
				break;

			case 3:
				if(verbose) printf("(%.2frpm) margin:%d\n", DENSITY3 / capacity[3], cap_margin[i]);
				break;
		}

		capacity[i] -= capacity_margin + extra_capacity_margin;
	}

	motor_speed = (float)((DENSITY3 / (capacity[3] + capacity_margin + extra_capacity_margin))
							+(DENSITY2 / (capacity[2] + capacity_margin + extra_capacity_margin))
							+(DENSITY1 / (capacity[1] + capacity_margin + extra_capacity_margin))
							+(DENSITY0 / (capacity[0] + capacity_margin + extra_capacity_margin)) ) / 4;

	//printf("--------------------------------------------------\n");
	printf("Motor speed: ~%.2f RPM.\n", motor_speed);
	printf("Track capacity margin: %d\n", capacity_margin + extra_capacity_margin);

	if( (motor_speed > 320) || (motor_speed < 280))
	{
		printf("\n\nERROR!\nDrive speed out of range.\nCheck motor, write-protect, or bad media.\n");
		exit(0);
	}
}

void
init_aligned_disk(CBM_FILE fd)
{
	int track;

	/* write all 0x55 */
	printf("\nWiping/Unformatting...\n");
	for (track = start_track; track <= end_track; track += 1)
	{
		// step head
		step_to_halftrack(fd, track);

		// write all $55 bytes
		send_mnib_cmd(fd, FL_FILLTRACK, NULL, 0);
		burst_write(fd, 0x55);
		burst_read(fd);
	}

	/* drive code version, timers can hang w/o interrupts too long */
	printf("Sync sweep...\n");
	send_mnib_cmd(fd, FL_ALIGNDISK, NULL, 0);
	burst_write(fd, 0x00);
	burst_read(fd);
	printf("Attempted sweep-aligned tracks\n");
}

