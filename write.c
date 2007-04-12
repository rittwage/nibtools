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
master_disk(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
	#define LEADER  0x100

	int track, length, i;
	BYTE rawtrack[NIB_TRACK_LENGTH + LEADER];

	for (track = start_track; track <= end_track; track += track_inc)
	{
		if(mode == MODE_WRITE_RAW)
		{
			length = track_length[track];
			printf("\n%4.1f: (", (float) track / 2);
			printf("%d", track_density[track] & 3);

			if ( (track_density[track]&3) != speed_map_1541[(track / 2) - 1])
				printf("!=%d", speed_map_1541[(track / 2) - 1]);

			printf(":%d) ", length);

			if (track_density[track] & BM_NO_SYNC)
				printf(" NOSYNC ");
			else if (track_density[track] & BM_FF_TRACK)
				printf(" KILLER ");

			if(track_length[track] == 0)
			{
				printf(" [missing - skipped]");
				continue;
			}
		}
		else
			length = process_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		/* zero out empty tracks entirely */
		if( (length == NIB_TRACK_LENGTH) && (track_density[track] & BM_NO_SYNC) )
		{
				unformat_track(fd, track);
				continue;
		}

		/* replace 0x00 bytes by 0x01, as 0x00 indicates end of track */
		replace_bytes(track_buffer + (track * NIB_TRACK_LENGTH), length, 0x00, 0x01);

		/* add filler so track is completely erased */
		memset(rawtrack, 0x55, sizeof(rawtrack));

		/* insert one very short sync */
		if(! (track_density[track] & BM_NO_SYNC))
			memset(rawtrack + LEADER - 2, 0xff, 2);

		/* append real track data */
		memcpy(rawtrack + LEADER, track_buffer + (track * NIB_TRACK_LENGTH), length);

		/* handle short tracks that won't 'loop overwrite' existing data */
		if(length + LEADER < capacity[track_density[track] & 3] - CAPACITY_MARGIN)
		{
				memset(rawtrack + length + LEADER, 0x55, (capacity[track_density[track] & 3] - CAPACITY_MARGIN) - length);
				printf("[PAD:%d]", (capacity[track_density[track] & 3] - CAPACITY_MARGIN) - length);
				length = capacity[track_density[track] & 3] - CAPACITY_MARGIN;
		}

		/* step to destination track and set density */
		step_to_halftrack(fd, track);
		set_density(fd, track_density[track]&3);

		/* burst send track */
		for (i = 0; i < 10; i ++)
		{
			send_mnib_cmd(fd, FL_WRITENOSYNC);
			cbm_parallel_burst_write(fd, (__u_char)((align_disk) ? 0xfb : 0x00));

			if (!cbm_parallel_burst_write_track(fd, rawtrack, length + LEADER))
			{
				//putchar('?');
				fflush(stdin);
				cbm_parallel_burst_read(fd);
				msleep(500);
				//printf("%c ", test_par_port(fd)? '+' : '-');
				test_par_port(fd);
			}
			else
				break;
		}
	}
}

void
write_raw(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
	int track;
	BYTE density;
	BYTE trackbuf[NIB_TRACK_LENGTH];
	char testfilename[16];
	FILE *trkin;
	int length;

	motor_on(fd);
	if (auto_capacity_adjust) adjust_target(fd);

	for (track = start_track; track <= end_track; track += track_inc)
	{
		// read in raw track at density (in filename)
		for (density = 0; density <= 3; density++)
		{
			sprintf(testfilename, "raw/tr%.1fd%d", (float) track/2, density);
			if ((trkin = fopen(testfilename, "rb"))) break;
		}

		if (trkin)
		{
			memset(trackbuf, 0x55, sizeof(trackbuf));
			fseek(trkin, 0, SEEK_END);
			length = ftell(trkin);
			rewind(trkin);
			fread(trackbuf, length, 1, trkin); // @@@SRT: check success
			fclose(trkin);

			memcpy(track_buffer + (track * NIB_TRACK_LENGTH), trackbuf, length);
			track_density[track] = density;
			track_length[track] = process_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], length);
		}
	}
	master_disk(fd, track_buffer, track_density, track_length);
	step_to_halftrack(fd, 18 * 2);
}

void
unformat_disk(CBM_FILE fd)
{
	int track;

	motor_on(fd);
	set_density(fd, 2);

	printf("\nUnformatting...\n\n");
	printf("00000000011111111112222222222333333333344\n");
	printf("12345678901234567890123456789012345678901\n");
	printf("-----------------------------------------\n");

	for (track = start_track; track <= end_track; track += track_inc)
	{
		printf("X");
		unformat_track(fd, track);
	}
	printf("\n");
}

void
unformat_track(CBM_FILE fd, int track)
{
	// step head
	step_to_halftrack(fd, track);

	// write all $0 bytes
	send_mnib_cmd(fd, FL_ZEROTRACK);
	cbm_parallel_burst_read(fd);
}

/* This routine measures track capacity at all densities */
void
adjust_target(CBM_FILE fd)
{
	int i, j;
	unsigned int cap[DENSITY_SAMPLES];
	int run_total;
	//BYTE track_dens[4] = { 35*2, 30*2, 24*2, 17*2 };

	printf("\nTesting track capacity at each density\n");
	printf("--------------------------------------------------\n");

	for (i = 0; i <= 3; i++)
	{
		//step_to_halftrack(fd, track_dens[i]);
		step_to_halftrack(fd, start_track);

		set_bitrate(fd, i);

		printf("Density %d: ", i);

		for(j = 0, run_total = 0; j < DENSITY_SAMPLES; j++)
		{
			cap[j] = track_capacity(fd);
			printf("%d ", cap[j]);
			run_total += cap[j];
		}

		capacity[i] = run_total / DENSITY_SAMPLES;

		//printf("(min:%d, max:%d)", capacity_min[i], capacity_max[i]);

		switch(i)
		{
			case 0: printf("[%.2f RPM]\n",DENSITY0 / capacity[0]); break;
			case 1: printf("[%.2f RPM]\n",DENSITY1 / capacity[1]); break;
			case 2: printf("[%.2f RPM]\n",DENSITY2 / capacity[2]); break;
			case 3: printf("[%.2f RPM]\n",DENSITY3 / capacity[3]); break;
		}
	}

	motor_speed = (float)((DENSITY3 / capacity[3]) +
										(DENSITY2 / capacity[2]) +
										(DENSITY1 / capacity[1]) +
										(DENSITY0 / capacity[0])) / 4;

	printf("--------------------------------------------------\n");
	printf("Drive motor speed average: %.2f RPM.\n", motor_speed);
}

void
init_aligned_disk(CBM_FILE fd)
{
	int track;
	BYTE sync[2];
	BYTE pattern[0x2000];

	memset(pattern, 0x55, 0x2000);
	sync[0] = sync[1] = 0xff;
	set_bitrate(fd, 2);

	/* write all 0x55 */
	printf("\nWiping/Unformatting disk\n");
	for (track = start_track; track <= end_track; track += track_inc)
	{
		send_mnib_cmd(fd, FL_STEPTO);
		cbm_parallel_burst_write(fd, (BYTE)track);
		cbm_parallel_burst_read(fd);

		send_mnib_cmd(fd, FL_WRITENOSYNC);
		cbm_parallel_burst_write(fd, 0);
		cbm_parallel_burst_write_track(fd, pattern, 0x2000);
		cbm_parallel_burst_read(fd);
	}

	/* write short syncs */
	printf("Aligning syncs\n");
	for (track = start_track; track <= end_track; track += track_inc)
	{
		send_mnib_cmd(fd, FL_STEPTO);
		cbm_parallel_burst_write(fd, (BYTE)track);
		cbm_parallel_burst_read(fd);

		msleep( (int) (((200000 - 20000 + skew) * 300) / motor_speed) );

		send_mnib_cmd(fd, FL_WRITENOSYNC);
		cbm_parallel_burst_write(fd, 0);
		cbm_parallel_burst_write_track(fd, sync, sizeof(sync));
		cbm_parallel_burst_read(fd);
	}
	printf("Successfully aligned tracks on disk\n");
}
