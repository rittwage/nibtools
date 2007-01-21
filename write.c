/*
 * MNIB Write routines
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

static BYTE diskbuf[MAX_TRACKS_1571 * NIB_TRACK_LENGTH];
static int track_length[MAX_TRACKS_1571], track_density[MAX_TRACKS_1571];

static void write_halftrack(int halftrack, int density, int length,  BYTE * gcrdata);
static void master_disk(CBM_FILE fd);
static void unformat_track(CBM_FILE fd, int track);

static void
write_halftrack(int halftrack, int density, int length, BYTE * gcrdata)
{
	int defdens, orglen;
	int badgcr = 0;

	// double-check our sync-flag assumptions
	density = check_sync_flags(gcrdata, (density & 3), length);

	// standard density for comparison
	defdens = speed_map_1541[(halftrack / 2) - 1];

	// user display
	if(verbose) printf("\n%4.1f: (", (float) halftrack / 2);

	if (density & BM_NO_SYNC)
	{
		if(verbose) printf("NOSYNC:");
	}
	else if (density & BM_FF_TRACK)
	{
		// reset sync killer track length to 0
		length = 0;
		if(verbose) printf("KILLER:");
	}

	if(verbose)
	{
		printf("%d", density & 3);
		if (density != defdens) printf("!");
		printf(") [");
	}

	if (length > 0)
	{
		// handle bad GCR / weak bits
		badgcr = check_bad_gcr(gcrdata, length, fix_gcr);
		if (badgcr > 0)
		{
			if(verbose) printf("weak:%d ", badgcr);
		}

		// If our track contains sync, we reduce to a minimum of 16
		// (only 10 are required, technically)
		orglen = length;
		if (length > (capacity[density & 3] - CAPACITY_MARGIN) && !(density & BM_NO_SYNC) && reduce_syncs)
		{
			// then try to reduce sync within the track
			if (length > capacity[density & 3] - CAPACITY_MARGIN)
				length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0xff);

			if (length < orglen)
			{
				if(verbose) printf("rsync:%d ", orglen - length);
			}
		}

		// We could reduce gap bytes ($55 and $AA) here too,
		orglen = length;
		if (length > (capacity[density & 3] - CAPACITY_MARGIN) && reduce_gaps)
		{
			length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0x55);
			length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0xaa);

			if (length < orglen)
			{
				if(verbose) printf("rgaps:%d ", orglen - length);
			}
		}

		// reduce weak bit runs (experimental)
		orglen = length;
		if (length > (capacity[density & 3] - CAPACITY_MARGIN) && badgcr > 0 && reduce_weak)
		{
			length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0x00);

			if (length < orglen)
			{
				if(verbose) printf("rweak:%d ", orglen - length);
			}
		}

		// still not small enough, we have to truncate the end
		orglen = length;
		if (length > capacity[density & 3] - CAPACITY_MARGIN)
		{
			length = capacity[density & 3] - CAPACITY_MARGIN;

			if (length < orglen)
			{
				if(verbose) printf("trunc:%d ", orglen - length);
			}
			else
			{
				if(verbose)
					printf("\nHad to truncate track %d by %d bytes.", halftrack / 2, orglen - length);
			}
		}

		// handle short tracks
		orglen = length;
		if(length < capacity[density & 3] - CAPACITY_MARGIN)
		{
			memset(gcrdata + length, 0x55, capacity[density & 3] - CAPACITY_MARGIN - length);
			length = capacity[density & 3] - CAPACITY_MARGIN;
			if(verbose) printf("pad:%d ", length - orglen);
		}
	}

	// if track is empty (unformatted) overfill with '0' bytes to simulate
	if (!length && (density & BM_NO_SYNC))
	{
		memset(gcrdata, 0x00, NIB_TRACK_LENGTH);
		length = NIB_TRACK_LENGTH;
	}

	// if it's a killer track, fill with sync
	if (!length && (density & BM_FF_TRACK))
	{
		memset(gcrdata, 0xff, NIB_TRACK_LENGTH);
		length = NIB_TRACK_LENGTH;
	}

	// replace 0x00 bytes by 0x01, as 0x00 indicates end of track
	replace_bytes(gcrdata, length, 0x00, 0x01);

	// write processed track to disk image
	track_length[halftrack] = length;
	track_density[halftrack] = density;
	memcpy(diskbuf + (halftrack * NIB_TRACK_LENGTH), gcrdata, length);

	if(verbose) printf("] (%d) ", length);

	// print out track alignment, as determined
	if (imagetype == IMAGE_NIB)
	{
		switch (align)
		{
		case ALIGN_NONE:
			if(verbose) printf("(none) ");
			break;
		case ALIGN_SEC0:
			if(verbose) printf("(sec0) ");
			break;
		case ALIGN_GAP:
			if(verbose) printf("(gap) ");
			break;
		case ALIGN_LONGSYNC:
			if(verbose) printf("(sync) ");
			break;
		case ALIGN_WEAK:
			if(verbose) printf("(weak) ");
			break;
		case ALIGN_VMAX:
			if(verbose) printf("(v-max) ");
			break;
		case ALIGN_AUTOGAP:
			if(verbose) printf("(auto) ");
			break;
		}
	}
}

static void
master_disk(CBM_FILE fd)
{
	int track, i;
	int align_delay;
	BYTE rawtrack[0x2800];

	if(track_inc == 2) /* no halftracks */
	{
		printf("\nWriting...\n\n");
		printf("00000000011111111112222222222333333333344\n");
		printf("12345678901234567890123456789012345678901\n");
		printf("-----------------------------------------\n");
	}
	else
		printf("\nWriting...\n\n");

	for (track = start_track; track <= end_track; track += track_inc)
	{
		if(track_inc == 1)  /* halftracks */
				printf("Track %.1f (%d) - %d bytes\n", (float) track/2, track_density[track]&3, track_length[track]);
		else
			printf("%d",track_density[track]&3);

		// skip empty tracks (raw mode tests)
		if ( mode == MODE_WRITE_RAW && track_length[track] == 0)
		{
			printf(".");
			continue;
		}

		// add filler so track is completely erased, then append track data
		memset(rawtrack, ((track_density[track] & BM_NO_SYNC) ? 0x55 : 0xff), sizeof(rawtrack));
		memcpy(rawtrack + 0x100, diskbuf + (track * NIB_TRACK_LENGTH), track_length[track]);

		// step to destination track and set density
		step_to_halftrack(fd, track);
		set_density(fd, track_density[track] & 3);

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

			if (!cbm_parallel_burst_write_track(fd, rawtrack, 0x100 + track_length[track]))
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
write_raw(CBM_FILE fd)
{
	int track, density;
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
			sprintf(testfilename, "raw/tr%dd%d", track / 2,
			  density);
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

			write_halftrack(track, density, length, trackbuf);
		}
	}
	master_disk(fd);
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

void
parse_disk(CBM_FILE fd, FILE * fpin, char *track_header)
{
	int track, density, dens_pointer, header_entry;
	BYTE buffer[NIB_TRACK_LENGTH];
	BYTE gcrdata[NIB_TRACK_LENGTH];
	int length, g64tracks, g64maxtrack;

	// clear our buffers
	memset(diskbuf, 0, sizeof(diskbuf));
	memset(track_length, 0, sizeof(track_length));
	memset(track_density, 0, sizeof(track_density));

	if (imagetype == IMAGE_NIB)
	{
		header_entry = 0;
		for (track = start_track; track <= end_track; track += track_inc)
		{
			// clear buffers
			memset(buffer, 0, sizeof(buffer));
			memset(gcrdata, 0, sizeof(gcrdata));

			/* get density from header or use default */
			density = (track_header[header_entry * 2 + 1]);
			header_entry++;

			// get track from file
			align = ALIGN_NONE;	// reset track alignment feedback
			fread(buffer, sizeof(buffer), 1, fpin); // @@@SRT: check success
			length = extract_GCR_track(gcrdata, buffer, &align,
			  force_align, capacity_min[density & 3], capacity_max[density & 3]);

			// write track
			write_halftrack(track, density, length, gcrdata);
		}
	}
	else if (imagetype == IMAGE_G64)
	{
		g64tracks = track_header[0];
		g64maxtrack = (BYTE) track_header[2] << 8 | (BYTE) track_header[1];
		printf("G64: %d tracks, %d bytes each", g64tracks,
		  g64maxtrack);

		// reduce tracks if > 41, we can't write 42 tracks
		if (g64tracks > 82) g64tracks = 82;

		dens_pointer = 0;
		for (track = start_track; track <= g64tracks; track += track_inc)
		{
			// clear buffers
			memset(buffer, 0, sizeof(buffer));
			memset(gcrdata, 0, sizeof(gcrdata));

			/* get density from header or use default */
			density = track_header[0x153 + dens_pointer];
			dens_pointer += 8;

			/* get length */
            fread(buffer, 2, 1, fpin); // @@@SRT: check success
			length = buffer[1] << 8 | buffer[0];

			/* get track from file */
			fread(gcrdata, g64maxtrack, 1, fpin); // @@@SRT: check success

			// write track
			write_halftrack(track, density, length, gcrdata);
		}
	}
	master_disk(fd);
	step_to_halftrack(fd, 18 * 2);
}

int
write_d64(CBM_FILE fd, FILE * fpin)
{
	int track, sector, sector_ref, density;
	BYTE buffer[256], gcrdata[NIB_TRACK_LENGTH];
	BYTE errorinfo[MAXBLOCKSONDISK];
	BYTE id[3] = { 0, 0, 0 };
	int length, error, d64size, last_track;
	char errorstring[0x1000], tmpstr[8];

	/* here we get to rebuild tracks from scratch */
	memset(errorinfo, SECTOR_OK, MAXBLOCKSONDISK);

	/* determine d64 image size */
	fseek(fpin, 0, SEEK_END);
	d64size = ftell(fpin);
	switch (d64size)
	{
	case (BLOCKSONDISK * 257):		/* 35 track image with errorinfo */
		fseek(fpin, BLOCKSONDISK * 256, SEEK_SET);
		fread(errorinfo, BLOCKSONDISK, 1, fpin); // @@@SRT: check success
		/* FALLTHROUGH */
	case (BLOCKSONDISK * 256):		/* 35 track image w/o errorinfo */
		last_track = 35;
		break;

	case (MAXBLOCKSONDISK * 257):	/* 40 track image with errorinfo */
		fseek(fpin, MAXBLOCKSONDISK * 256, SEEK_SET);
		fread(errorinfo, MAXBLOCKSONDISK, 1, fpin); // @@@SRT: check success
		/* FALLTHROUGH */
	case (MAXBLOCKSONDISK * 256):	/* 40 track image w/o errorinfo */
		last_track = 40;
		break;

	default:
		rewind(fpin);
		fprintf(stderr, "Bad d64 image size.\n");
		return 0;
	}

	// determine disk id from track 18 (offsets $165A2, $165A3)
	fseek(fpin, 0x165a2, SEEK_SET);
	fread(id, 2, 1, fpin); // @@@SRT: check success
	printf("\ndisk id: %s", id);

	rewind(fpin);

	sector_ref = 0;
	for (track = 1; track <= last_track; track++)
	{
		// clear buffers
		memset(gcrdata, 0x55, sizeof(gcrdata));
		errorstring[0] = '\0';

		for (sector = 0; sector < sector_map_1541[track]; sector++)
		{
			// get error and increase reference pointer in errorinfo
			error = errorinfo[sector_ref++];
			if (error != SECTOR_OK)
			{
				sprintf(tmpstr, " E%dS%d", error, sector);
				strcat(errorstring, tmpstr);
			}

			// read sector from file
			fread(buffer, 256, 1, fpin); // @@@SRT: check success

			// convert to gcr
			convert_sector_to_GCR(buffer,
			  gcrdata + (sector * 361), track, sector, id, error);
		}

		// calculate track length
		length = sector_map_1541[track] * 361;

		// use default densities for D64
		for (density = 3; track * 2 >= bitrate_range[density]; density--);

		// write track
		write_halftrack(track * 2, density, length, gcrdata);
		printf("%s", errorstring);
	}

	// "unformat" last 5 tracks on 35 track disk
	if (last_track == 35)
	{
		for (track = 36 * 2; track <= end_track; track += 2)
		{
			track_length[track] = NIB_TRACK_LENGTH;
			track_density[track] = 2;
			memset(diskbuf + (track * NIB_TRACK_LENGTH), 0x01, NIB_TRACK_LENGTH);
		}
	}

	master_disk(fd);
	step_to_halftrack(fd, 18 * 2);
	return (1);
}

/* This routine measures track capacity at all densities */
void
adjust_target(CBM_FILE fd)
{
	int i;
	unsigned int cap1, cap2;
	int track_dens[4] = { 35*2, 30*2, 24*2, 17*2 };

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
