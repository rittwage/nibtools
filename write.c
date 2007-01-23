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
	int track, length, i;
	int align_delay;
	BYTE rawtrack[NIB_TRACK_LENGTH + 0x100];

	for (track = start_track; track <= end_track; track += track_inc)
	{
		length = process_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		/* skip empty tracks (raw mode) */
		if ( (mode == MODE_WRITE_RAW) && (length == 0))
		{
			printf(".");
			continue;
		}

		// add filler so track is completely erased, then append track data
		memset(rawtrack, ((track_density[track] & BM_NO_SYNC) ? 0x55 : 0xff), sizeof(rawtrack));
		memcpy(rawtrack + 0x100, track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]);

		// step to destination track and set density
		step_to_halftrack(fd, track);
		set_density(fd, track_density[track]&3);

		// try to do track alignment through simple timers
		if((align_disk) && (auto_capacity_adjust))
		{
			/* subtract 28300ms overhead from one 200000ms rev;
			    adjust for motor speed and density;
			    doesn't seem to be a skew between densities 2 and 1.
			*/
			align_delay = (int) ((171700) + ((300 - motor_speed) * 600));

			if((track_density[track] & 3) == 2 || (track_density[track] & 3) ==1 )
				align_delay -= 1000;

			if((track_density[track] & 3) == 0)
				align_delay -= 2000;

			msleep(align_delay);
		}

		// send track
		for (i = 0; i < 10; i ++)
		{
			send_mnib_cmd(fd, FL_WRITENOSYNC);
			//cbm_parallel_burst_write(fd, (__u_char)((align_disk) ? 0xfb : 0x00));
			cbm_parallel_burst_write(fd, 0x00);

			if (!cbm_parallel_burst_write_track(fd, rawtrack, 0x100 + length))
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
	if (auto_capacity_adjust)
		adjust_target(fd);

	for (track = start_track; track <= end_track; track += track_inc)
	{
		// read in raw track at density (in filename)
		for (density = 0; density <= 3; density++)
		{
			sprintf(testfilename, "raw/tr%dd%d", track / 2, density);
			if ((trkin = fopen(testfilename, "rb")))
				break;
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
			process_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);
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
	int i;
	unsigned int cap1, cap2;
	BYTE track_dens[4] = { 35*2, 30*2, 24*2, 17*2 };

	printf("\nTesting track capacity at each density\n");
	printf("--------------------------------------------------\n");

	for (i = 0; i <= 3; i++)
	{
		step_to_halftrack(fd, track_dens[i]);

		set_bitrate(fd, i);

		cap1 = track_capacity(fd);
		cap2 = track_capacity(fd);

		capacity[i] = (cap1 + cap2) / 2;

		printf("Density %d: %d (min:%d, max:%d)", i,  capacity[i],
		        capacity_min[i], capacity_max[i]);

		if (capacity[i] < capacity_min[i] || capacity[i] > capacity_max[i])
		{
			printf(" [OUT OF RANGE]\n");
		}
		else if (capacity[i] < capacity_min[i] || capacity[i] > capacity_max[i])
		{
			printf("\nMotor speed is too far out of range.\n\n");
			printf("Possible problems:\n");
			printf("1) No disk in drive.\n");
			printf("2) Write protect is on.\n");
			printf("3) Disk is damaged.\n");
			printf("4) Drive needs adjusted, cleaned, or repaired.\n");
			exit(2);
		}
		else
		{
			switch(i)
			{
				case 0: printf(" [%.2f RPM]\n",DENSITY0 / capacity[0]); break;
				case 1: printf(" [%.2f RPM]\n",DENSITY1 / capacity[1]); break;
				case 2: printf(" [%.2f RPM]\n",DENSITY2 / capacity[2]); break;
				case 3: printf(" [%.2f RPM]\n",DENSITY3 / capacity[3]); break;
			}
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
	int track, track_delay;

	/*
	> $1172: a0 6c           LDY  #$6c    ; 2
	> $1174: a2 87           LDX  #$87    ; 2
	> $1176: c8              INY          ; 2
	> $1177: d0 fd           BNE  $1176    ; 2 (+1)
	> $1179: ca              DEX          ; 2
	> $117a: d0 fa           BNE  $1176    ; 2 (+1)
	>
	> The trouble with the BNE opcode is, that it has three different
	> cycle values, depending weather it branches or not, and how
	> far it branches.
	>
	> No branch: 2 cycles
	> Branch within the page: 3 cycles
	> Branch outside page: 4 cycles
	>
	> All branches are within the same pages, but sometimes
	> it doesn't branch, so the calculation becomes somewhat more
	> complicated.

	> 2 + 2 + (5 * 0x94 - 1)       // 0x2e7 / 743
	> + (5 * 0x100 - 1) * (0x86) + (5 * 0x87 - 1) // 2a01c / 172060
	>
	> = 0x2a303 = 172803 cycles
	>
	> t = 172803 / 985248 = 0.175390 seconds
	*/

	// adjust delay based on measured motor speed
	track_delay = (int) ((175390 * 300) / motor_speed);
	printf("Formatting track-aligned disk with calulated delay of %dus\n", track_delay);

	// make sure we are at highest bitrate
	set_bitrate(fd, 3);

	for (track = start_track; track <= end_track; track += track_inc)
	{
		step_to_halftrack(fd, track);
		msleep(track_delay);
		send_mnib_cmd(fd, FL_INITTRACK);
		cbm_parallel_burst_read(fd);
	}
}
