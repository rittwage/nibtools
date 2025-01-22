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

	//if(track_density[track] & BM_NO_SYNC)
	//	memset(rawtrack, 0x55, sizeof(rawtrack));
	//else
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
	// really only works on old parallel setup
	if((skew||align_disk) && (auto_capacity_adjust))
	{
		/* subtract overhead from one revolution; adjust for motor speed and density; */
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

		/* I think the +1 is because the last byte is not written 100%, but probably never matters */
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
	size_t badgcr, badgcr2, length, verlen, verlen2;
	BYTE verbuf1[NIB_TRACK_LENGTH], verbuf2[NIB_TRACK_LENGTH], verbuf3[NIB_TRACK_LENGTH], align;
	size_t gcr_match;
	char errorstring[0x1000];
	char fillbytesave;

	//if(track_inc==1) unformat_disk(fd);

	printf("Writing to disk");

	for (track=backwards?end_track:start_track; backwards?(track>=start_track):(track<=end_track); backwards?(track-=track_inc):(track+=track_inc))
	{
		/* double-check our sync-flag assumptions and process track for remaster */
		track_density[track] =
			check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		/* engineer killer track */
		if(track_density[track] & BM_FF_TRACK)
		{
				fill_track(fd, track, 0xFF);
				printf("\n%4.1f: KILLED!",  (float) track / 2);
				continue;
		}

		/* zero out empty tracks entirely */
		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]))
		{
				if(track_inc!=1)
				{
					fill_track(fd, track, 0x00);
					printf("\n%4.1f: UNFORMATTED!",  (float) track / 2);
				}
				continue;
		}

		/* user display */
		printf("\n%4.1f: (", (float)track/2);
		printf("%d", track_density[track]&3);
		if ((track_density[track]&3) != speed_map[track/2]) printf("!");
		printf(":%d) ", track_length[track]);
		if (track_density[track] & BM_NO_SYNC) printf("NOSYNC ");
		if (track_density[track] & BM_FF_TRACK) printf("KILLER ");
		printf("WRITE  ");

		/* loop last byte of track data for filler
		   we do this before processing track in case we get wrong byte */
		fillbytesave = fillbyte;
		if(fillbyte == 0xfe)
			fillbyte = track_buffer[(track * NIB_TRACK_LENGTH) + track_length[track] - 1];
		if(verbose) printf("[fill:$%x]", fillbyte);

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
		if(badgcr) printf("[weak:%d]", badgcr);

		verbose+=1;
		length = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);
		verbose-=1;

		master_track(fd, track_buffer, track_density, track, length);

		fillbyte = fillbytesave;

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
				verlen  = extract_GCR_track(verbuf2, verbuf1, &align, track/2, track_length[track], track_length[track]);
				verlen2 = extract_GCR_track(verbuf3, track_buffer+(track * NIB_TRACK_LENGTH), &align, track/2, track_length[track], track_length[track]);

				printf("\n      (%d:%d) VERIFY ", track_density[track]&3, verlen);
				fprintf(fplog, "\n      (%d:%d) VERIFY ", track_density[track]&3, verlen);

				// Fix bad GCR in tracks for compare
				badgcr = check_bad_gcr(verbuf2, track_length[track]);
				if(verbose>1) printf("(badgcr=%.4d:", badgcr);
				badgcr2 = check_bad_gcr(verbuf3, track_length[track]);
				if(verbose>1) printf("%.4d)", badgcr2);

				// compare raw gcr data
				gcr_match = compare_tracks(verbuf3, verbuf2, verlen, verlen, 1, errorstring);
				//printf(" (match:%.4d) ", (int)gcr_match);
				fprintf(fplog, " (match:%.4d) ", (int)gcr_match);

				if(gcr_match >= length-10)
				{
					printf("OK (%.4d/%.4d) ",gcr_match,length);
					verified=1;
				}
				else
				{
					retries++;
					printf("Retry %d (%.4d/%.4d) ",retries,gcr_match,length);
					fill_track(fd, track, 0x00);
					master_track(fd, track_buffer, track_density, track, length);
				}
				if(((track>70)&&(retries>=3))||(retries>=10))
				{
					printf("\nWrite verify FAILED - Odd data or bad media!\n");
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
	/* this routine can write all 1's and/or all 0's alternatively to try to both
		fix old media into working again, and wiping all data */
	int track, i;

	motor_on(fd);
	set_density(fd, 2);

	printf("Wiping/Unformatting...\n");

	for (track = start_track; track <= end_track; track += 1/*track_inc*/)
	{
		if(verbose) printf("\n%4.1f:",  (float) track/2);
		for(i=0;i<unformat_passes; i++)
		{
			if(verbose) printf(" [Pass:%d] ",i+1);
			//if(read_killer) fill_track(fd, track, 0xFF);
			fill_track(fd, track, 0x00);
		}
		if(verbose) printf("UNFORMATTED!");
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

	printf("Testing drive motor speed for 100 loops.\n");
	printf("--------------------------------------------------\n");
	printf("Track 41.5 will be destroyed!\n");

	motor_on(fd);
	step_to_halftrack(fd, 83);
	set_bitrate(fd, 2);

	for (i=0; i<100; i++)
	{
		cap = track_capacity(fd);
		printf("%d/100 - Speed = %0.2fRPM\n",i+1,(float)DENSITY2 / cap);
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

	printf("Testing track capacity/motor speed\n");
	printf("----------------------------------\n");

	for (i = 0; i <= 3; i++)
	{
		cap_high[i] = 0;
		cap_low[i] = 0xffff;

		if( (start_track < track_dens[i]) && (end_track > track_dens[i]))
			step_to_halftrack(fd, track_dens[i]);
		else
			step_to_halftrack(fd, start_track);

		set_bitrate(fd, (BYTE)i);

		printf("%d: ", i);

		for(j = 0, run_total = 0; j < DENSITY_SAMPLES; j++)
		{
			cap[j] = track_capacity(fd);
			printf("%d ", cap[j]);
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
				printf("(%.2frpm) margin:%d\n", (float)DENSITY0 / capacity[0], cap_margin[i]);
				break;

			case 1:
				printf("(%.2frpm) margin:%d\n", (float)DENSITY1 / capacity[1], cap_margin[i]);
				break;

			case 2:
				printf("(%.2frpm) margin:%d\n", (float)DENSITY2 / capacity[2], cap_margin[i]);
				break;

			case 3:
				printf("(%.2frpm) margin:%d\n", (float)DENSITY3 / capacity[3], cap_margin[i]);
				break;
		}

		capacity[i] -= capacity_margin + extra_capacity_margin;
	}

	motor_speed = (float)(((float)DENSITY3 / (capacity[3] + capacity_margin + extra_capacity_margin))
							+((float)DENSITY2 / (capacity[2] + capacity_margin + extra_capacity_margin))
							+((float)DENSITY1 / (capacity[1] + capacity_margin + extra_capacity_margin))
							+((float)DENSITY0 / (capacity[0] + capacity_margin + extra_capacity_margin)) ) / 4;

	printf("Motor speed: ~%.2f RPM.\n", motor_speed);
	printf("Track capacity margin: %d\n", capacity_margin + extra_capacity_margin);

	if( (motor_speed > 320) || (motor_speed < 280))
	{
		printf("\n\nERROR!\nDrive speed out of range.\nCheck motor, write-protect, or bad media.\n");
		exit(0);
	}
	printf("----------------------------------\n");
}

void
init_aligned_disk(CBM_FILE fd)
{
	int track;

	/* write all 0x55 */
	printf("Wiping/Unformatting...\n");
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

