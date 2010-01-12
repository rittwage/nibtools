/*
 * NIBTOOL read routines
 * Copyright 2001-2005 Markus Brenner <markus(at)brenner(dot)de>
 * and Pete Rittwage <peter(at)rittwage(dot)com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"

static BYTE diskid[3];

BYTE read_halftrack(CBM_FILE fd, int halftrack, BYTE * buffer)
{
	BYTE density;
    int i, newtrack;
	static int lasttrack = -1;

	newtrack = (lasttrack == halftrack) ? 0 : 1;
	lasttrack = halftrack;

	step_to_halftrack(fd, halftrack);

	if(newtrack)
	{
		printf("\n%4.1f: ", (float) halftrack / 2);
		fprintf(fplog, "\n%4.1f: ", (float) halftrack / 2);
	}
	else
	{
		printf("\n      ");
		fprintf(fplog, "\n      ");
	}

	if(halftrack/2 > 35)
	{
		density = scan_track(fd, halftrack);
	}
	else	 if(force_density)
	{
		density = speed_map[halftrack/2];
		printf("{DEFAULT }");
	}
	else
	{
		// we scan for the disk density
		density = scan_track(fd, halftrack);
	}

	/* output current density */
	printf("(%d",density&3);
	fprintf(fplog,"(%d",density&3);

	if ( (density&3) != speed_map[halftrack/2])
		printf("!=%d", speed_map[halftrack/2]);

	if(density & BM_FF_TRACK)
	{
		printf(" KILLER");
		fprintf(fplog, " KILLER");
	}

	if(density & BM_NO_SYNC)
	{
		printf(" NOSYNC");
		fprintf(fplog," NOSYNC");
	}

	printf(") ");
	fprintf(fplog,") ");

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
		if((ihs) && (!(density & BM_NO_SYNC)))
			send_mnib_cmd(fd, FL_READIHS, NULL, 0);
		else
		{
			if ((density & BM_NO_SYNC) || (density & BM_FF_TRACK) || (force_nosync))
				send_mnib_cmd(fd, FL_READWOSYNC, NULL, 0);
			else
				send_mnib_cmd(fd, FL_READNORMAL, NULL, 0);
		}
		cbm_parallel_burst_read(fd);

		if (!cbm_parallel_burst_read_track(fd, buffer, NIB_TRACK_LENGTH))
		{
			// If we got a timeout, reset the port before retrying.
			printf("(timeout) ");
			fflush(stdout);
			cbm_parallel_burst_read(fd);
			delay(500);
			//printf("%c ", test_par_port(fd)? '+' : '-');
			cbm_parallel_burst_read(fd);
		}
		else
			break;
	}

	return (density);
}

BYTE paranoia_read_halftrack(CBM_FILE fd, int halftrack, BYTE * buffer)
{
	BYTE buffer1[NIB_TRACK_LENGTH];
	BYTE buffer2[NIB_TRACK_LENGTH];
	BYTE cbuffer1[NIB_TRACK_LENGTH];
	BYTE cbuffer2[NIB_TRACK_LENGTH];
	BYTE *cbufn, *cbufo, *bufn, *bufo;
	BYTE align;
	size_t leno, lenn;
	BYTE denso, densn;
	size_t i, l, badgcr, retries, errors;
	char errorstring[0x1000], diffstr[80];

	badgcr = 0;
	errors = 0;
	retries = 1;
	denso = 0;
	densn = 0;
	leno = 0;
	lenn = 0;
	bufn = buffer1;
	bufo = buffer2;
	cbufn = cbuffer1;
	cbufo = cbuffer2;

	diffstr[0] = '\0';
	errorstring[0] = '\0';

	// First pass at normal track read
	for (l = 0; l <= error_retries; l ++)
	{
		memset(bufo, 0, NIB_TRACK_LENGTH);
		denso = read_halftrack(fd, halftrack, bufo);

		// Find track cycle and length
		memset(cbufo, 0, NIB_TRACK_LENGTH);
		leno = extract_GCR_track(cbufo, bufo, &align, halftrack/2, capacity_min[denso & 3], capacity_max[denso & 3]);

		// if we have a killer track, exit processing
		if(denso & BM_FF_TRACK)
		{
			printf("[Killer Track] ");
			fprintf(fplog, "[Killer Track] %s (%d)", errorstring, leno);
			memcpy(buffer, bufo, NIB_TRACK_LENGTH);
			return (denso);
		}

		// If we get nothing we are on an empty track (unformatted)
		if (!leno)
		{
			printf("[Unformatted Track] ");
			fprintf(fplog, "[Unformatted Track] %s (%d)", errorstring, leno);
			memcpy(buffer, bufo, NIB_TRACK_LENGTH);
			return (denso);
		}

		// if we get less than what a track holds,
		// try again, probably bad read or a bad GCR match
		if (leno < capacity_min[denso & 3] - CAP_MIN_ALLOWANCE)
		{
			printf("Short Read! (%d) ", leno);
			fprintf(fplog, "[%d<%d!] ", leno, capacity_min[denso & 3] - CAP_MIN_ALLOWANCE);
			//if(l < (error_retries - 3)) l = error_retries - 3;
			continue;
		}

		// if we get more than capacity
		// try again to make sure it's intentional
		if (leno > capacity_max[denso & 3] + CAP_MIN_ALLOWANCE)
		{
			printf("Long Read! (%d) ", leno);
			fprintf(fplog, "[%d>%d!] ", leno, capacity_max[denso & 3] + CAP_MIN_ALLOWANCE);
			//if(l < (error_retries - 3)) l = error_retries - 3;
			continue;
		}

		printf("%d ", leno);
		fprintf(fplog, "%d ", leno);

		// check for CBM DOS errors
		errors = check_errors(cbufo, leno, halftrack, diskid, errorstring);
		fprintf(fplog, "%s", errorstring);

		// if we got all good sectors we dont retry
		if (errors == 0) break;

		// if all bad sectors (protection) we only retry once
		if (errors == sector_map[halftrack/2])
		{
			if(l < (error_retries - 3))
				l = error_retries - 3;
		}
		else // else we are probably looping for a read retry
			printf("%s ", errorstring);

		if(l < error_retries - 1) printf("(retry) ");
	}

	// If there are a lot of errors, the track probably doesn't contain
	// any CBM sectors (protection)
	if ((errors == sector_map[halftrack/2]) || (halftrack > 70))
	{
		printf("[Non-Standard Format] ");
		fprintf(fplog, "%s ", errorstring);
	}
	else
	{
		printf("[%d Errors] ", errors);
		fprintf(fplog, "[%d Errors] ", errors);
	}

	// Fix bad GCR in track for compare
	badgcr = check_bad_gcr(cbufo, leno);

	if(track_match)
	{
		// Try to verify our read

		// Don't bother to compare unformatted or bad data
		if (leno == NIB_TRACK_LENGTH)
			retries = 0;

		// normal data, verify
		for (i = 0; i < retries; i++)
		{
			memset(bufn, 0, NIB_TRACK_LENGTH);
			densn = read_halftrack(fd, halftrack, bufn);

			memset(cbufn, 0, NIB_TRACK_LENGTH);
			lenn = extract_GCR_track(cbufn, bufn, &align, halftrack/2, capacity_min[densn & 3], capacity_max[densn & 3]);

			printf("%d ", lenn);
			fprintf(fplog, "%d ", lenn);

			// fix bad GCR in track for compare
			badgcr = check_bad_gcr(cbufn, lenn);

			// compare raw gcr data, unreliable
			if (compare_tracks(cbufo, cbufn, leno, lenn, 1, errorstring))
			{
				printf("[Raw GCR Match] ");
				fprintf(fplog, "[Raw GCR Match] ");
				break;
			}
			else
				fprintf(fplog, "%s", errorstring);

			// compare sector data
			if (compare_sectors(cbufo, cbufn, leno, lenn, diskid, diskid, halftrack, errorstring) ==
				sector_map[halftrack/2])
			{
				printf("[Data Match] ");
				fprintf(fplog, "[Data Match] ");
				break;
			}
			else
				fprintf(fplog, "%s", errorstring);
		}
	}

	if (badgcr)
	{
		printf("(bad/weak:%d)", badgcr);
		fprintf(fplog, "(bad/weak:%d) ", badgcr);
	}

	//printf("\n");
	fprintf(fplog, "%s (%d)", errorstring, leno);
	memcpy(buffer, bufo, NIB_TRACK_LENGTH);
	return denso;
}

int
read_floppy(CBM_FILE fd, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
    int track, errors = 0;
    char errorstring[0x1000];

	printf("\n");
	fprintf(fplog,"\n");

	if(!rawmode) get_disk_id(fd);

	for (track = start_track; track <= end_track; track += track_inc)
	{
		track_density[track] = paranoia_read_halftrack(fd, track, track_buffer + (track * NIB_TRACK_LENGTH));

		if(track <= 70)
				errors += check_errors(track_buffer + (track * NIB_TRACK_LENGTH), NIB_TRACK_LENGTH, track, diskid, errorstring);
	}
	printf("\n\nTotal CBM Errors: %d\n",errors);
	step_to_halftrack(fd, 18*2);

	return 1;
}

void
write_nb2(CBM_FILE fd, char * filename)
{
	BYTE density;
	FILE * fpout;
	int track, i, header_entry, pass, pass_density;
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
	sprintf(header, "MNIB-1541-RAW%c%c%c", 2, 0, 1);

	if (fwrite(header, sizeof(header), 1, fpout) != 1) {
		printf("unable to write NB2 header\n");
		exit(2);
	}

	get_disk_id(fd);

	header_entry = 0;
	for (track = start_track; track <= end_track; track += track_inc)
	{
		memset(buffer, 0, sizeof(buffer));

		density = read_halftrack(fd, track, buffer);
		//density = paranoia_read_halftrack(fd, track, buffer);
		printf("\n");

		header[0x10 + (header_entry * 2)] = (BYTE) track;
		header[0x10 + (header_entry * 2) + 1] = density;
		header_entry++;

		step_to_halftrack(fd, track);

		/* make 16 passes of track, four for each density */
		for(pass_density = 0; pass_density < 4; pass_density ++)
		{
			printf("%4.1f: (%d) ", (float) track / 2, pass_density);
			fprintf(fplog, "%4.1f: (%d) ", (float) track / 2, pass_density);

			for(pass = 0; pass < 4; pass ++)
			{
				set_density(fd, pass_density);

				for (i = 0; i < 10; i++)
				{
					send_mnib_cmd(fd, FL_READWOSYNC, NULL, 0);
					cbm_parallel_burst_read(fd);

					if (!cbm_parallel_burst_read_track(fd, buffer, NIB_TRACK_LENGTH))
					{
						// If we got a timeout, reset the port before retrying.
						//putchar('?');
						fflush(stdout);
						cbm_parallel_burst_read(fd);
						delay(500);
						//printf("%c ", test_par_port(fd)? '+' : '-');
						cbm_parallel_burst_read(fd);
					}
					else
						break;
				}

				/* save track to disk */
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
		density = read_halftrack(fd, 18 * 2, buffer);

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
	density = (BYTE)set_default_bitrate(fd, track);
	send_mnib_cmd(fd, FL_SCANKILLER, NULL, 0);
	killer_info = cbm_parallel_burst_read(fd);

	if (killer_info & BM_FF_TRACK)
			return (density | killer_info);

	for (bin = 0; bin < 4; bin++)
		density_major[bin] = density_stats[bin] = 0;

	for (i = 0; i < 10; i++)
	{
		/* Use bitrate close to default for scan */
		set_bitrate(fd, density);

		send_mnib_cmd(fd, FL_SCANDENSITY, NULL, 0);

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
			if(verbose) printf("{%d:%3d/%2d}",i,density_stats[i],density_major[i]);
			dens_detected = 1;
		}

		fprintf(fplog,"{%d:%3d/%2d}",i,density_stats[i],density_major[i]);
	}

	if((!dens_detected) && (verbose))
		printf("{ NOGCR? }");

	// if the default density flagged a good detect, skip calculations
	if ((density_major[density] > 0) && (!killer_info))
		goto rescan;

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

rescan:
	/* Set bitrate to the discovered density and scan again for NOSYNC/KILLER */
	set_bitrate(fd, density);

	for(i=0; i<3; i++)
	{
		send_mnib_cmd(fd, FL_SCANKILLER, NULL, 0);
		killer_info = cbm_parallel_burst_read(fd);

		if (killer_info & BM_NO_SYNC)
			return (density | killer_info);
	}
	return (density | killer_info);
}
