/*
 * NIBTOOL write routines
 * Copyright 2001-2007 Pete Rittwage <peter(at)rittwage(dot)com>
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
master_track(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, int track, size_t track_length)
{
	#define LEADER  0x10
	int i;
	static size_t skewbytes = 0;
	BYTE rawtrack[NIB_TRACK_LENGTH * 2];

	/* unformat track with 0x55 (01010101)
	    some of this is the "leader" which is overwritten by wraparound */
	memset(rawtrack, 0x55, sizeof(rawtrack));

	/* apply skew, if specified */
	if(skew)
	{
		skewbytes += skew * (capacity[track_density[track]&3] / 200);

		if(skewbytes > NIB_TRACK_LENGTH)
			skewbytes = skewbytes - NIB_TRACK_LENGTH;

		printf(" {skew=%d} ", skewbytes);
	}

	/* check that our first sync is long enough (if the track has sync)
		and if not, lengthen it */
	if( (track_density[track] & BM_NO_SYNC) ||
		(align_map[track/2] == ALIGN_AUTOGAP) ||
		((track_buffer[track * NIB_TRACK_LENGTH] == 0xff) && (track_buffer[(track * NIB_TRACK_LENGTH) + 1] == 0xff)) )
	{
			/* merge in our track data normally */
			memcpy(rawtrack + LEADER + skewbytes,  track_buffer + (track * NIB_TRACK_LENGTH), track_length);
	}
	else
	{
			/* merge in our track data with an extended sync mark */
			memset(rawtrack + LEADER + skewbytes,  0xff, 2);
			memcpy(rawtrack + LEADER + skewbytes  + 2,  track_buffer + (track * NIB_TRACK_LENGTH), track_length);
			track_length += 2;
			printf("{presync} ");
	}

	/*
	printf("[%.2x%.2x%.2x] ", track_buffer[track*NIB_TRACK_LENGTH],
			track_buffer[track*NIB_TRACK_LENGTH+1],track_buffer[track*NIB_TRACK_LENGTH+2]);
	*/

	/* handle short tracks */
	if(track_length < capacity[track_density[track] & 3])
	{
			printf("[pad:%d]", capacity[track_density[track] & 3] - track_length);
			track_length = capacity[track_density[track] & 3];
	}

	/* replace 0x00 bytes by 0x01, as 0x00 indicates end of track */
	replace_bytes(rawtrack, sizeof(rawtrack), 0x00, 0x01);

	/* step to destination track and set density */
	step_to_halftrack(fd, track);
	set_density(fd, track_density[track]&3);

	/* burst send track */
	for (i = 0; i < 10; i ++)
	{
		if(ihs)
			send_mnib_cmd(fd, FL_WRITEIHS, NULL, 0);
		else
			send_mnib_cmd(fd, FL_WRITENOSYNC, NULL, 0);

		cbm_parallel_burst_write(fd, (__u_char)((align_disk) ? 0xfb : 0x00));

		if (cbm_parallel_burst_write_track(fd, rawtrack, track_length + LEADER + skewbytes))
			break;
		else
		{
			//putchar('?');
			printf("(timeout) ");
			fflush(stdin);
			cbm_parallel_burst_read(fd);
			//msleep(500);
			//printf("%c ", test_par_port(fd)? '+' : '-');
			test_par_port(fd);
		}
	}
}

void
master_disk(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track;
	size_t badgcr, length;

	for (track = start_track; track <= end_track; track += track_inc)
	{
		/* double-check our sync-flag assumptions and process track for remaster */
		track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		/* engineer killer track */
		if(track_density[track] & BM_FF_TRACK)
		{
				kill_track(fd, track);
				printf("\n%4.1f: KILLER",  (float) track / 2);
				continue;
		}

		/* zero out empty tracks entirely */
		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH)))
		{
				zero_track(fd, track);
				printf("\n%4.1f: UNFORMATTED",  (float) track / 2);
				continue;
		}

		badgcr = check_bad_gcr(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]);
		length = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);
		printf("[badgcr:%d] ", badgcr);

		master_track(fd, track_buffer, track_density, track, length);
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

	for (track = start_track; track <= end_track; track += track_inc)
	{
		printf("\n%4.1f:", (float) track / 2);

		// read in raw track at density (in filename)
		for (density = 3; density >= 0; density--)
		{
			sprintf(testfilename, "raw/tr%.1fd%d", (float) track/2, density);

			if( (trkin = fopen(testfilename, "rb")) )
			{
				printf(" [%s] ", testfilename);
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
			if(length == 0) length = NIB_TRACK_LENGTH;

			/* process track */
			memcpy(track_buffer + (track * NIB_TRACK_LENGTH), trackbuf, NIB_TRACK_LENGTH);
			track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), density, length);
			printf(" (%d", track_density[track] & 3);

			if ( (track_density[track]&3) != speed_map[track/2])
				printf("!=%d", speed_map[track/2]);

			if (track_density[track] & BM_NO_SYNC)
					printf(":NOSYNC");
			else if (track_density[track] & BM_FF_TRACK)
				printf(":KILLER");

			printf(") (%d) ", length);

			/* truncate the end if needed (reduce tail) */
			if ( (length > capacity[density & 3]) && (length != NIB_TRACK_LENGTH) )
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
	/* this routine writes all 1's and all 0's alternatively to try to both
		fix old media into working again, and wiping all data
	*/
	int track, i;

	motor_on(fd);
	set_density(fd, 2);

	printf("\nUnformatting...\n\n");
	printf("00000000011111111112222222222333333333344\n");
	printf("12345678901234567890123456789012345678901\n");
	printf("-----------------------------------------\n");

	for (track = start_track; track <= end_track; track += track_inc)
	{
		for(i=0;i<unformat_passes; i++)
		{
			kill_track(fd,track);
			zero_track(fd, track);
		}
		printf("X");
	}
	printf("\n");
}

void kill_track(CBM_FILE fd, int track)
{
	// step head
	step_to_halftrack(fd, track);

	// write all $ff bytes
	send_mnib_cmd(fd, FL_FILLTRACK, NULL, 0);
	cbm_parallel_burst_write(fd, 0xff);  // 0xff byte is all sync "killer" track
	cbm_parallel_burst_read(fd);
}

void
zero_track(CBM_FILE fd, int track)
{
	// step head
	step_to_halftrack(fd, track);

	// write all $0 bytes
	send_mnib_cmd(fd, FL_FILLTRACK, NULL, 0);
	cbm_parallel_burst_write(fd, 0x0);  // 0x00 byte is "unformatted"
	cbm_parallel_burst_read(fd);
}

/* This routine measures track capacity at all densities */
void
adjust_target(CBM_FILE fd)
{
	int i, j;
	int cap[DENSITY_SAMPLES];
	int cap_high[4], cap_low[4], cap_margin[4];
	int run_total;
	int capacity_margin;
	BYTE track_dens[4] = { 35*2, 30*2, 24*2, 17*2 };

	printf("\nTesting track capacity at each density\n");
	printf("--------------------------------------------------\n");

	for (i = 0; i <= 3; i++)
	{
		cap_high[i] = 0;
		cap_low[i] = 0xffff;

		if( (start_track < track_dens[i]) && (end_track > track_dens[i]))
			step_to_halftrack(fd, track_dens[i]);
		else
			step_to_halftrack(fd, start_track);

		set_bitrate(fd, i);

		printf("Density %d: ", i);

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

		capacity[i] -= capacity_margin + EXTRA_CAPACITY_MARGIN;

		switch(i)
		{
			case 0: printf("(%.2frpm) margin:%d\n",DENSITY0 / capacity[0], cap_margin[i]); break;
			case 1: printf("(%.2frpm) margin:%d\n",DENSITY1 / capacity[1], cap_margin[i]); break;
			case 2: printf("(%.2frpm) margin:%d\n",DENSITY2 / capacity[2], cap_margin[i]); break;
			case 3: printf("(%.2frpm) margin:%d\n",DENSITY3 / capacity[3], cap_margin[i]); break;
		}
	}

	motor_speed = (float)((DENSITY3 / capacity[3]) +
										(DENSITY2 / capacity[2]) +
										(DENSITY1 / capacity[1]) +
										(DENSITY0 / capacity[0])) / 4;

	printf("--------------------------------------------------\n");
	printf("Drive motor speed average: %.2f RPM.\n", motor_speed);
	printf("Track capacity margin: %d\n",capacity_margin + EXTRA_CAPACITY_MARGIN);

	if( (motor_speed > 310) || (motor_speed < 290))
	{
		printf("\n\nERROR!\nDrive speed out of range.\nCheck motor, write-protect, or bad media.\n");
		exit(0);
	}
}

void
init_aligned_disk(CBM_FILE fd)
{
	int track, skewtime;
	BYTE sync[2];
	BYTE pattern[0x2000];

	memset(pattern, 0x55, 0x2000);
	sync[0] = sync[1] = 0xff;
	set_bitrate(fd, 2);

	skewtime = ((200000 - 20000) * 300) / motor_speed;

	/* write all 0x55 */
	printf("\nPreparing tracks...\n");
	for (track = start_track; track <= end_track; track += track_inc)
	{
		step_to_halftrack(fd, track);
		send_mnib_cmd(fd, FL_WRITENOSYNC, NULL, 0);

		cbm_parallel_burst_write(fd, 0);

		if(cbm_parallel_burst_write_track(fd, pattern, 0x2000))
			break;
		else
		{
			printf("\nTimeout during alignment- alignment failed!\n");
			cbm_parallel_burst_read(fd);
			exit(0);
		}
	}

	/* drive code version */
	//send_mnib_cmd(fd, FL_ALIGNDISK, NULL, 0);
	//cbm_parallel_burst_write(fd, skewtime/1000);
	//cbm_parallel_burst_read(fd);
	//return;

	/* write short syncs */
	printf("Aligning syncs\n");
	for (track = end_track; track >= start_track; track -= track_inc)
	{
		step_to_halftrack(fd, track);
		msleep( skewtime);
		send_mnib_cmd(fd, FL_WRITENOSYNC, NULL, 0);
		cbm_parallel_burst_write(fd, 0);
		if(cbm_parallel_burst_write_track(fd, sync, sizeof(sync)))
			break;
		else
		{
			printf("\nTimeout during alignment- alignment failed!\n");
			exit(0);
		}
	}
	printf("Attempted time-aligned tracks on disk\n");
}
