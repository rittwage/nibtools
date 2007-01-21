/*
 * MNIB Read routines
 * Copyright 2001-2005 Markus Brenner <markus(at)brenner(dot)de>
 * and Pete Rittwage <peter(at)rittwage(dot)com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"

static BYTE diskid[3];

BYTE
read_halftrack(CBM_FILE fd, int halftrack, BYTE * buffer, int forced_density)
{
	BYTE density;
    int i, timeout, newtrack;
	static int lasttrack = -1;
    static BYTE lastdensity;

	timeout = 0;
	newtrack = (lasttrack == halftrack) ? 0 : 1;
	lasttrack = halftrack;

	// we only do all this for a new track, not for retries
	if(forced_density != 0xff)
	{
		density = forced_density & 0xFF;
	}
	else if (newtrack)
	{
		step_to_halftrack(fd, halftrack);

		printf("%4.1f: ", (float) halftrack / 2);
		fprintf(fplog, "%4.1f: ", (float) halftrack / 2);

		if(default_density && halftrack <= 70) // still scans density for all tracks > 35
		{
			lastdensity = density = speed_map_1541[(halftrack / 2) - 1];
			printf("{DEFAULT }");
		}
		else
		{
			// we scan for the disk density
			lastdensity = density = scan_track(fd, halftrack);
		}

		/* output current density */
		printf(" (%d) ",density&3);
		fprintf(fplog," (%d) ",density&3);
	}
	else
			density = lastdensity;

	// bail if we don't want to read killer tracks
	// some drives/disks timeout
	if ((density & BM_FF_TRACK) && (!read_killer))
	{
		memset(buffer, 0xff, NIB_TRACK_LENGTH);
		return (density);
	}

	set_density(fd, density & 3);

	for (i = 0; i < 10; i++)
	{
		// read track
		if ((density & BM_NO_SYNC) || (density & BM_FF_TRACK))
			send_mnib_cmd(fd, FL_READWOSYNC);
		else
			send_mnib_cmd(fd, FL_READNORMAL);

		cbm_parallel_burst_read(fd);
		timeout = cbm_parallel_burst_read_track(fd, buffer, NIB_TRACK_LENGTH);

		// If we got a timeout, reset the port before retrying.
		if (!timeout)
		{
			putchar('?');
			fflush(stdout);
			cbm_parallel_burst_read(fd);
			delay(500);
			printf("%c ", test_par_port(fd)? '+' : '-');
		}
		else
			break;
	}

	return (density);
}

static BYTE
paranoia_read_halftrack(CBM_FILE fd, int halftrack, BYTE * buffer, int forced_density)
{
	BYTE buffer1[NIB_TRACK_LENGTH];
	BYTE buffer2[NIB_TRACK_LENGTH];
	BYTE cbuffer1[NIB_TRACK_LENGTH];
	BYTE cbuffer2[NIB_TRACK_LENGTH];
	BYTE *cbufn, *cbufo, *bufn, *bufo;
	int leno, lenn, densn, i, l, badgcr,errors, retries, short_read, long_read;
    BYTE denso;
	char errorstring[0x1000], diffstr[80];

	badgcr = 0;
	errors = 0;
	retries = 1;
	short_read = 0;
	long_read = 0;
	bufn = buffer1;
	bufo = buffer2;
	cbufn = cbuffer1;
	cbufo = cbuffer2;

	diffstr[0] = '\0';
	errorstring[0] = '\0';

	if (!error_retries)
		error_retries = 1;

	// First pass at normal track read
	for (l = 0; l < error_retries; l++)
	{
		memset(bufo, 0, NIB_TRACK_LENGTH);
		denso = read_halftrack(fd, halftrack, bufo, forced_density);

		// Find track cycle and length
		memset(cbufo, 0, NIB_TRACK_LENGTH);
		leno = extract_GCR_track(cbufo, bufo, &align, force_align,
		  capacity_min[denso & 3], capacity_max[denso & 3]);

		// if we have a killer track and are ignoring them, exit processing
		if((denso & BM_FF_TRACK) && (!read_killer))
			break;

		// If we get nothing (except t18), we are on an empty
		// track (unformatted)
		if (!leno)
		{
			printf("[UNFORMATTED]\n");
			fprintf(fplog, "%s (%d)\n", errorstring, leno);
			memcpy(buffer, bufo, NIB_TRACK_LENGTH);
			return (denso);
		}

		// if we get less than what a track holds,
		// try again, probably bad read or a weak match
		if (leno < capacity_min[denso & 3] - 155)
		{
			printf("<! ");
			fprintf(fplog, "[%d<%d!] ", leno,
			  capacity_min[denso & 3] - 155);
			l--;
			if (short_read++ > error_retries)
				break;
			continue;
		}

		// if we get more than capacity
		// try again to make sure it's intentional
		if (leno > capacity_max[denso & 3] + 255)
		{
			printf("!> ");
			fprintf(fplog, "[%d>%d!] ", leno,
			  capacity_max[denso & 3] + 255);
			l--;
			if (long_read++ > error_retries)
				break;
			continue;
		}

		printf("%d ", leno);
		fprintf(fplog, "%d ", leno);

		// check for CBM DOS errors
		errors = check_errors(cbufo, leno, halftrack, diskid, errorstring);
		fprintf(fplog, "%s", errorstring);

		// if we got all good sectors we dont retry
		if (errors == 0)
			break;

		// if all bad sectors (protection) we only retry once
		//if (errors == sector_map_1541[halftrack/2])
		//	l = error_retries - 1;
	}

	// Give some indication of disk errors, unless it's all errors
	errors = check_errors(cbufo, leno, halftrack, diskid, errorstring);

	// If there are a lot of errors, the track probably doesn't contain
	// any DOS sectors (protection)
	if (errors == sector_map_1541[halftrack/2])
	{
		printf("[NDOS] ");
		fprintf(fplog, "%s ", errorstring);
	}
	else if (errors > 0)
	{
		// probably old-style intentional error(s)
		printf("%s ", errorstring);
		fprintf(fplog, "%s ", errorstring);
	}
	else
	{
		// this is all good CBM DOS-style sectors
		printf("[DOS] ");
		fprintf(fplog, "[DOS] ");
	}

	// Fix bad GCR in track for compare
	badgcr = check_bad_gcr(cbufo, leno, 1);

	if(track_match)
	{
		// Try to verify our read
		printf("- ");

		// Don't bother to compare unformatted or bad data
		if (leno == NIB_TRACK_LENGTH)
			retries = 0;

		// normal data, verify
		for (i = 0; i < retries; i++)
		{
			memset(bufn, 0, NIB_TRACK_LENGTH);
			densn = read_halftrack(fd, halftrack, bufn, forced_density);

			memset(cbufn, 0, NIB_TRACK_LENGTH);
			lenn = extract_GCR_track(cbufn, bufn, &align, force_align,
			  capacity_min[densn & 3], capacity_max[densn & 3]);

			printf("%d ", lenn);
			fprintf(fplog, "%d ", lenn);

			// fix bad GCR in track for compare
			badgcr = check_bad_gcr(cbufn, lenn, 1);

			// compare raw gcr data, unreliable
			if (compare_tracks(cbufo, cbufn, leno, lenn, 1, errorstring))
			{
				printf("[RAW MATCH] ");
				fprintf(fplog, "[RAW MATCH] ");
				break;
			}
			else
				fprintf(fplog, "%s", errorstring);

			// compare sector data
			if (compare_sectors(cbufo, cbufn, leno, lenn, diskid, diskid,
			  halftrack, errorstring))
			{
				printf("[SEC MATCH] ");
				fprintf(fplog, "[SEC MATCH] ");
				break;
			}
			else
				fprintf(fplog, "%s", errorstring);
		}
	}

	if (badgcr)
	{
		printf("(weak:%d)", badgcr);
		fprintf(fplog, "(weak:%d) ", badgcr);
	}

	printf("\n");
	fprintf(fplog, "%s (%d)\n", errorstring, leno);
	memcpy(buffer, bufo, NIB_TRACK_LENGTH);
	return denso;
}

void
read_nib(CBM_FILE fd, char * filename)
{
    BYTE track, density;
    FILE * fpout;
	int header_entry;
	char header[0x100];
	BYTE buffer[NIB_TRACK_LENGTH];

	printf("\n");
	fprintf(fplog,"\n");

	/* create output file */
	if ((fpout = fopen(filename, "wb")) == NULL)
	{
		fprintf(stderr, "Couldn't create output file %s!\n", filename);
		exit(2);
	}

	/* write initial NIB-header */
	memset(header, 0x00, sizeof(header));

	/* header now contains whether halftracks were read */
	if(track_inc == 1)
		sprintf(header, "MNIB-1541-RAW%c%c%c", 2, 0, 1);
	else
		sprintf(header, "MNIB-1541-RAW%c%c%c", 2, 0, 0);

	if (fwrite(header, sizeof(header), 1, fpout) != 1) {
		printf("unable to write NIB header\n");
		exit(2);
	}

	get_disk_id(fd);

	header_entry = 0;
	for (track = start_track; track <= end_track; track += track_inc)
	{
		memset(buffer, 0, sizeof(buffer));

		// density = read_halftrack(fd, track, buffer, 0xff);
		density = paranoia_read_halftrack(fd, track, buffer, 0xff);
		header[0x10 + (header_entry * 2)] = track;
		header[0x10 + (header_entry * 2) + 1] = density;
		header_entry++;

		/* process and save track to disk */
		if (fwrite(buffer, sizeof(buffer), 1, fpout) != 1)
		{
			printf("unable to rewrite NIB track data\n");
			fclose(fpout);
			exit(2);
		}
		fflush(fpout);
	}

	/* fill NIB-header */
	rewind(fpout);
	if (fwrite(header, sizeof(header), 1, fpout) != 1) {
		printf("unable to rewrite NIB header\n");
		exit(2);
	}

	fclose(fpout);
	step_to_halftrack(fd, 18 * 2);
}

void
read_nb2(CBM_FILE fd, char * filename)
{
    BYTE track, density;
    FILE * fpout;
	int header_entry, pass, pass_density;
	BYTE buffer[NIB_TRACK_LENGTH];
	char header[0x100];

	printf("\n");
	fprintf(fplog,"\n");

	/* create output file */
	if ((fpout = fopen(filename, "wb")) == NULL)
	{
		fprintf(stderr, "Couldn't create output file %s!\n", filename);
		exit(2);
	}

	/* write initial NIB-header */
	memset(header, 0x00, sizeof(header));

	/* header now contains whether halftracks were read */
	if(track_inc == 1)
		sprintf(header, "MNIB-1541-RAW%c%c%c", 3, 0, 1);
	else
		sprintf(header, "MNIB-1541-RAW%c%c%c", 3, 0, 0);

	if (fwrite(header, sizeof(header), 1, fpout) != 1) {
		printf("unable to write NB2 header\n");
		exit(2);
	}

	get_disk_id(fd);

	header_entry = 0;
	for (track = start_track; track <= end_track; track += track_inc)
	{
		memset(buffer, 0, sizeof(buffer));

		// density = read_halftrack(fd, track, buffer, 0xff);
		density = paranoia_read_halftrack(fd, track, buffer, 0xff);
		header[0x10 + (header_entry * 2)] = track;
		header[0x10 + (header_entry * 2) + 1] = density;
		header_entry++;

		/* make 16 passes of track, four for each density */
		for(pass_density = 0; pass_density < 4; pass_density ++)
		{
			printf("%4.1f: (%d) ", (float) track / 2, pass_density);
			fprintf(fplog, "%4.1f: (%d) ", (float) track / 2, pass_density);

			for(pass = 0; pass < 4; pass ++)
			{
				density = read_halftrack(fd, track, buffer, pass_density | BM_NO_SYNC);

				/* process and save track to disk */
				if (fwrite(buffer, sizeof(buffer), 1, fpout) != 1)
				{
					printf("unable to rewrite NIB track data\n");
					fclose(fpout);
					exit(2);
				}
				fflush(fpout);
				printf("%d ", pass+1);
			}

			printf("\n");
			fprintf(fplog,"\n");
		}
	}

	/* fill NB2-header */
	rewind(fpout);
	if (fwrite(header, sizeof(header), 1, fpout) != 1)
	{
		printf("unable to rewrite NB2 header\n");
		exit(2);
	}

	fclose(fpout);

	step_to_halftrack(fd, 18 * 2);
}

void get_disk_id(CBM_FILE fd)
{
		int density;
		BYTE buffer[NIB_TRACK_LENGTH];

		/* read track 18 for ID checks*/
		density = read_halftrack(fd, 18 * 2, buffer, 0xff);

		/* print cosmetic disk id */
		memset(diskid, 0, sizeof(diskid));
		printf("\nCosmetic Disk ID: ");

		if(!extract_cosmetic_id(buffer, diskid))
			fprintf(stderr, "[Cannot find directory sector!]\n");
		else
		{
			printf("'%c%c'\n", diskid[0], diskid[1]);
			fprintf(fplog, "\nCID: '%c%c'\n", diskid[0], diskid[1]);
		}

		/* determine format id for e11 checks */
		memset(diskid, 0, sizeof(diskid));
		printf("Format Disk ID: ");

		if (!extract_id(buffer, diskid))
			fprintf(stderr, "[Cannot find directory sector!]\n");
		else
		{
			printf("'%c%c'\n", diskid[0], diskid[1]);
			fprintf(fplog, "FID: '%c%c'\n", diskid[0], diskid[1]);
		}

		printf("\n");
		fprintf(fplog,"\n");
}


int
read_d64(CBM_FILE fd, char * filename)
{
	FILE *fpout;
	int density, track, sector;
	int csec;		/* compare sector variable */
	int blockindex, save_errorinfo, save_40_errors, save_40_tracks;
	int retry;
	BYTE buffer[0x2100];
	BYTE *cycle_start;	/* start position of cycle     */
	BYTE *cycle_stop;	/* stop  position of cycle + 1 */
	BYTE id[3];
	BYTE rawdata[260];
	BYTE *sectordata, *d64data, *d64ptr;
	BYTE errorinfo[MAXBLOCKSONDISK], errorcode;
	int sector_count[21];	/* number of different sector data read */
	int sector_occur[21][16];	/* how many times was this sector data read? */
	BYTE sector_error[21][16];	/* type of error on this sector data */
	int sector_use[21];	/* best data for this sector so far */
	int sector_max[21];	/* # of times the best sector data has occured */
	int goodtrack, goodsector;
	int any_sectors;	/* any valid sectors on track at all? */
	int blocks_to_save;

	/* create output file */
	if ((fpout = fopen(filename, "wb")) == NULL)
	{
		fprintf(stderr, "Couldn't create output file %s!\n", filename);
		exit(2);
	}

	d64data = calloc(MAXBLOCKSONDISK, 256);
	sectordata = calloc(16 * 21, 260);

	if((!d64data) || (!sectordata))
	{
		printf("not enough memory for buffers");
		if(d64data) free(d64data);
		if(sectordata) free(sectordata);
		return 0;
	}

	blockindex = 0;
	save_errorinfo = 0;
	save_40_errors = 0;
	save_40_tracks = 0;

	density = read_halftrack(fd, 18 * 2, buffer, 0xff);
	printf("\n");
	if (!extract_id(buffer, id))
	{
		fprintf(stderr, "Cannot find directory sector.\n");
		free(d64data);
		free(sectordata);
		return 0;
	}

	d64ptr = d64data;
	for (track = 1; track <= 40; track += 1)
	{
		/* no sector data read in yet */
		for (sector = 0; sector < 21; sector++)
			sector_count[sector] = 0;

		any_sectors = 0;
		for (retry = 0; retry < 16; retry++)
		{
			goodtrack = 1;
			density = read_halftrack(fd, 2 * track, buffer, 0xff);

			cycle_start = buffer;
			find_track_cycle(&cycle_start, &cycle_stop,
			  capacity_min[density & 3],
			  capacity_max[density & 3]);

			for (sector = 0; sector < sector_map_1541[track]; sector++)
			{
				sector_max[sector] = 0;
				/* convert sector to free sector buffer */
				errorcode =
				  convert_GCR_sector(cycle_start, cycle_stop,
				  rawdata, track, sector, id);

				if (errorcode == SECTOR_OK)
					any_sectors = 1;

				/* check, if identical sector has been read before */
				for (csec = 0; csec < sector_count[sector]; csec++)
				{
					if (memcmp(sectordata + (21 * csec + sector) * 260,
					  rawdata, 260) == 0 &&
					  sector_error[sector][csec] == errorcode)
					{
						sector_occur[sector][csec] += 1;
						break;
					}
				}
				if (csec == sector_count[sector])
				{
					/* sectordaten sind neu, kopieren, zaehler erhoehen */
					memcpy(sectordata + (21 * csec + sector) * 260, rawdata,
					  260);
					sector_occur[sector][csec] = 1;
					sector_error[sector][csec] = errorcode;
					sector_count[sector] += 1;
				}

				goodsector = 0;
				for (csec = 0; csec < sector_count[sector]; csec++)
				{
					if (sector_occur[sector][csec] -
					  ((sector_error[sector][csec] == SECTOR_OK) ? 0 : 8) >
					  sector_max[sector])
					{
						sector_use[sector] = csec;
						sector_max[sector] = csec;
					}

					if (sector_occur[sector][csec] -
					  ((sector_error[sector][csec] == SECTOR_OK) ? 0 : 8) >
					  (retry / 2) + 1)
					{
						goodsector = 1;
					}
				}
				if (goodsector == 0)
					goodtrack = 0;
			}

			if (goodtrack == 1)
				break;
			if (retry == 1 && any_sectors == 0)
				break;
		}

		/* keep the best data for each sector */
		for (sector = 0; sector < sector_map_1541[track]; sector++)
		{
			printf("%d", sector);

			memcpy(d64ptr,
			  sectordata + 1 + (21 * sector_use[sector] + sector) * 260, 256);
			d64ptr += 256;

			errorcode = sector_error[sector][sector_use[sector]];
			errorinfo[blockindex] = errorcode;

			if (errorcode != SECTOR_OK)
			{
				if (track <= 35)
					save_errorinfo = 1;
				else
					save_40_errors = 1;
			}
			else if (track > 35)
			{
				save_40_tracks = 1;
			}

			/* screen information */
			if (errorcode == SECTOR_OK)
				printf(" ");
			else
				printf("%d", errorcode);
			blockindex++;
		}
		printf("\n");

	}

	blocks_to_save = (save_40_tracks) ? MAXBLOCKSONDISK : BLOCKSONDISK;

	if (fwrite(d64data, blocks_to_save * 256, 1, fpout) != 1)
	{
		fprintf(stderr, "Cannot write d64 data.\n");
		free(d64data);
		free(sectordata);
		return 0;
	}

	if (save_errorinfo == 1)
	{
                assert(sizeof(errorinfo) >= (size_t)blocks_to_save);
		if (fwrite(errorinfo, blocks_to_save, 1, fpout) != 1)
		{
			fprintf(stderr, "Cannot write sector data.\n");
			free(d64data);
			free(sectordata);
			return 0;
		}
	}

	free(d64data);
	free(sectordata);
	fclose(fpout);
	return 1;
}

/* $152b Density Scan */
BYTE
scan_track(CBM_FILE fd, int track)
{
	BYTE density, killer_info;
	int bin, i, dens_detected=0;
	BYTE count;
	BYTE density_major[4], iMajorMax; /* 50% majorities for bit rate */
	BYTE density_stats[4], iStatsMax; /* total occurrences */

	/* Scan for killer track */
	density = set_default_bitrate(fd, track);
	send_mnib_cmd(fd, FL_SCANKILLER);
	killer_info = cbm_parallel_burst_read(fd);

	if (killer_info & BM_FF_TRACK)
	{
			printf("KILLER! ");
			fprintf(fplog, "KILLER! ");
			return (density | killer_info);
	}

	for (bin = 0; bin < 4; bin++)
		density_major[bin] = density_stats[bin] = 0;

	/* Use medium bitrate for scan */
	set_bitrate(fd, 2);

	/* we have to sample density 2 tracks more because they are sometimes on the edge of 1 and 2 by this routine. */
	for (i = 0; i < ((density == 2) ? 20 : 10); i++)
	{
		send_mnib_cmd(fd, FL_SCANDENSITY);

		/* Floppy sends statistic data in reverse bit-rate order */
		for (bin = 3; bin >= 0; bin--)
		{
			count = cbm_parallel_burst_read(fd);
			if (count >= 0x40)
				density_major[bin]++;

			density_stats[bin] += count;
		}
		cbm_parallel_burst_read(fd);
	}

	for(i = 0; i <= 3; i++)
	{
		if(density_major[i] > 1)
		{
			printf("{%d:%3d/%2d}",i,density_stats[i],density_major[i]);
			dens_detected = 1;
		}

		fprintf(fplog,"{%d:%3d/%2d}",i,density_stats[i],density_major[i]);
	}

	if(!dens_detected)
		printf("{ NONGCR }");

	// if the default density flagged a good detect, just return it now
	if ((density_major[density] > 0) && (!killer_info))
		return (density | killer_info);

	// calculate
	iMajorMax = iStatsMax = 0;
	for (bin = 1; bin < 4; bin++)
	{
		if (density_major[bin] > density_major[iMajorMax])
			iMajorMax = (BYTE) bin;
		if (density_stats[bin] > density_stats[iStatsMax])
			iStatsMax = (BYTE) bin;
	}

	if (density_major[iMajorMax] > 0)
		density = iMajorMax;
	else if (density_stats[iStatsMax] > density_stats[density])
		density = iStatsMax;

	/* Set bitrate to the discovered density and scan again for NOSYNC/KILLER */
	set_bitrate(fd, density);
	send_mnib_cmd(fd, FL_SCANKILLER);
	killer_info = cbm_parallel_burst_read(fd);

	if (killer_info & BM_NO_SYNC)
	{
		printf(" NOSYNC!");
		fprintf(fplog, " NOSYNC!");
	}

	return (density | killer_info);
}
