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
	#define LEADER  0x100
	int i;
	size_t skewbytes = 0;
	BYTE rawtrack[NIB_TRACK_LENGTH * 2];

	/* engineer killer track */
	if(track_density[track] & BM_FF_TRACK)
	{
			kill_track(fd, track);
			return;
	}

	/* zero out empty tracks entirely */
	if( (track_length == NIB_TRACK_LENGTH) && (track_density[track] & BM_NO_SYNC) )
	{
			zero_track(fd, track);
			return;
	}

	/* unformat track with 0x55 (01010101) or fillbyte
	    some of this is the "leader" which is overwritten
	    some 1571's don't like a lot of 0x00 bytes, they see phantom sync, etc.
	*/
	if(track_density[track] & BM_NO_SYNC)
		memset(rawtrack, 0x55, sizeof(rawtrack));
	else
		memset(rawtrack, fillbyte, sizeof(rawtrack));

	/* apply skew, if specified*/
	if(skew)
	{
		skewbytes = skew * (capacity[track_density[track] & 3] / 200000);
		printf(" {skew=%d} ", skewbytes);
	}

	/* merge in our track data */
	memcpy(rawtrack + LEADER + skewbytes,  track_buffer + (track * NIB_TRACK_LENGTH), track_length);

	/* handle short tracks that won't 'loop overwrite' existing data */
	if(track_length + LEADER + skewbytes < capacity[track_density[track] & 3] - CAPACITY_MARGIN)
	{
			printf("[pad:%d]", (capacity[track_density[track] & 3] - CAPACITY_MARGIN) - track_length);
			track_length = capacity[track_density[track] & 3] - CAPACITY_MARGIN;
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

		if (!cbm_parallel_burst_write_track(fd, rawtrack, track_length + LEADER + skewbytes))
		{
			//putchar('?');
			printf("(timeout) ");
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

void
master_disk(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track;
	size_t badgcr, length;

	for (track = start_track; track <= end_track; track += track_inc)
	{
		/* double-check our sync-flag assumptions and process track for remaster */
		track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);
		badgcr = check_bad_gcr(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]);
		length = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);
		printf("[badgcr: %d] ", badgcr);
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
			memset(trackbuf, 0x55, sizeof(trackbuf));
			fseek(trkin, 0, SEEK_END);
			length = ftell(trkin);
			rewind(trkin);
			fread(trackbuf, length, 1, trkin); // @@@SRT: check success
			fclose(trkin);

			/* process track */
			memcpy(track_buffer + (track * NIB_TRACK_LENGTH), trackbuf, length);
			track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH), density, length);
			printf(" (%d", track_density[track] & 3);

			if ( (track_density[track]&3) != speed_map[track/2])
				printf("!=%d", speed_map[track/2]);

			printf(":%d) ", length);

			if (track_density[track] & BM_NO_SYNC)
					printf(" NOSYNC ");
			else if (track_density[track] & BM_FF_TRACK)
				printf(" KILLER ");

			master_track(fd, track_buffer, track_density, track, length);
		}
		else
			printf(" [missing - skipped]");
	}
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
		zero_track(fd, track);
		kill_track(fd,track);
		zero_track(fd,track);
	}
	printf("\n");
}

void kill_track(CBM_FILE fd, int track)
{
	BYTE trackbuf[NIB_TRACK_LENGTH];

	// step head
	step_to_halftrack(fd, track);
	set_density(fd, 2);

	// write 0xFF $2000 times
	memset(trackbuf, 0xff, NIB_TRACK_LENGTH);
	send_mnib_cmd(fd, FL_WRITENOSYNC, NULL, 0);
	cbm_parallel_burst_write(fd, 0x00);
	cbm_parallel_burst_write_track(fd, trackbuf, NIB_TRACK_LENGTH);
}

void
zero_track(CBM_FILE fd, int track)
{
	// step head
	step_to_halftrack(fd, track);

	// write all $0 bytes
	send_mnib_cmd(fd, FL_ZEROTRACK, NULL, 0);
	cbm_parallel_burst_read(fd);
}

/* This routine measures track capacity at all densities */
void
adjust_target(CBM_FILE fd)
{
	int i, j;
	unsigned int cap[DENSITY_SAMPLES];
	int run_total;
	BYTE track_dens[4] = { 35*2, 30*2, 24*2, 17*2 };

	printf("\nTesting track capacity at each density\n");
	printf("--------------------------------------------------\n");

	for (i = 0; i <= 3; i++)
	{
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

	if( (motor_speed > 310) || (motor_speed < 295))
	{
		printf("\n\nERROR!\nDrive speed out of range.\nCheck motor, write-protect, or bad media.\n");
		exit(0);
	}
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
		step_to_halftrack(fd, track);

		send_mnib_cmd(fd, FL_WRITENOSYNC, NULL, 0);
		cbm_parallel_burst_write(fd, 0);
		cbm_parallel_burst_write_track(fd, pattern, 0x2000);
		cbm_parallel_burst_read(fd);
	}

	/* write short syncs */
	printf("Aligning syncs\n");
	for (track = start_track; track <= end_track; track += track_inc)
	{
		step_to_halftrack(fd, track);

		msleep( (int) (((200000 - 20000 + skew) * 300) / motor_speed) );

		send_mnib_cmd(fd, FL_WRITENOSYNC, NULL, 0);
		cbm_parallel_burst_write(fd, 0);
		cbm_parallel_burst_write_track(fd, sync, sizeof(sync));
		cbm_parallel_burst_read(fd);
	}
	printf("Successfully aligned tracks on disk\n");
}
