/*
	fileio.c - (C) Pete Rittwage
	---
	contains routines used by nibtools to read/write files on the host

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"

int read_nib(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
    /*	reads contents of a NIB file into 'buffer' */

	int track, nibsize, numtracks;
	int header_entry = 0;
	char header[0x100];
	BYTE nibdata[0x2000];
	FILE *fpin;

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		fprintf(stderr, "Couldn't open input file %s!\n", filename);
		return 0;
	}

	/* Determine number of tracks in image (crudely estimated by filesize) */
	fseek(fpin, 0, SEEK_END);
	nibsize = ftell(fpin);
	numtracks = (nibsize - NIB_HEADER_SIZE) / NIB_TRACK_LENGTH;

	if(numtracks <= 42)
	{
		end_track = (numtracks * 2) + 1;
		track_inc = 2;
	}
	else
	{
		printf("\nImage contains halftracks!\n");
		end_track = numtracks + 1;
		track_inc = 1;
	}

	printf("\n%d track image (filesize = %d bytes)\n", numtracks, nibsize);
	rewind(fpin);

	/* gather mem and read header */
	if (fread(header, sizeof(header), 1, fpin) != 1) {
		printf("unable to read NIB header\n");
		return 0;
	}

	for (track = start_track; track <= end_track; track += track_inc)
	{

		/* get density from header or use default */
		track_density[track] = (BYTE)(header[0x10 + (header_entry * 2) + 1]);
		header_entry++;

		/* get track from file */
		if(fread(nibdata, NIB_TRACK_LENGTH, 1, fpin) !=1)
		{
			printf("error reading NIB file\n");
			return 0;
		}

		/* process track cycle */
		track_length[track] = extract_GCR_track(track_buffer + (track * NIB_TRACK_LENGTH), nibdata,
			&align, force_align, capacity_min[track_density[track]&3], capacity_max[track_density[track]&3]);

		/* output some specs */
		printf("%4.1f: (",(float) track / 2);
		if(track_density[track] & BM_NO_SYNC) printf("NOSYNC!");
		if(track_density[track] & BM_FF_TRACK) printf("KILLER!");
		printf("%d:%d)\n", track_density[track]&3, track_length[track]  );
	}
	fclose(fpin);
	printf("Successfully loaded NIB file\n");
	return 1;
}

int read_g64(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
    /*	reads contents of a G64 file into 'buffer'  */

	int track, g64maxtrack;
	int dens_pointer = 0;
	int g64tracks;
	BYTE header[0x2ac];
	BYTE length_record[2];
	FILE *fpin;

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		fprintf(stderr, "Couldn't open input file %s!\n", filename);
		return 0;
	}

	if (fread(header, sizeof(header), 1, fpin) != 1) {
		printf("unable to read G64 header\n");
		return 0;
	}

	g64tracks = (char) header[0x9];
	g64maxtrack = (BYTE)header[0xb] << 8 | (BYTE)header[0xa];
	printf("\nG64: %d tracks, %d bytes each\n", g64tracks, g64maxtrack);

	/* reduce tracks if > 41, we can't write 42 tracks */
	if (g64tracks > 82) g64tracks = 82;

	for (track = start_track; track <= g64tracks; track += track_inc)
	{
		/* get density from header */
		track_density[track] = header[0x9 + 0x153 + dens_pointer];
		dens_pointer += 8;

		/* get length */
		if(fread(length_record, 2, 1, fpin) != 1)
		{
			printf("unable to read G64 track length\n");
			return 0;
		}
		track_length[track] = length_record[1] << 8 | length_record[0];

		/* get track from file */
		if(fread(track_buffer + (track * NIB_TRACK_LENGTH), g64maxtrack, 1, fpin) !=1)
		{
			printf("error reading G64 file\n");
			return 0;
		}
	}
	fclose(fpin);
	printf("Successfully loaded G64 file\n");
	return 1;
}

int
read_d64(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
	/* reads contents of a D64 file into 'buffer'  */
	int track, sector, sector_ref;
	BYTE buffer[256], gcrdata[NIB_TRACK_LENGTH];
	BYTE errorinfo[MAXBLOCKSONDISK];
	BYTE id[3] = { 0, 0, 0 };
	int error, d64size, last_track;
	char errorstring[0x1000], tmpstr[8];
	FILE *fpin;

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		fprintf(stderr, "Couldn't open input file %s!\n", filename);
		return 0;
	}

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
	printf("\ndisk id: %s\n", id);
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
		track_length[track*2] = sector_map_1541[track] * 361;

		// use default densities for D64
		track_density[track*2] = speed_map_1541[track-1];

		// write track
		memcpy(track_buffer + (track * 2 * NIB_TRACK_LENGTH), gcrdata, track_length[track*2]);
		printf("%s", errorstring);
	}

	// "unformat" last 5 tracks on 35 track disk
	if (last_track == 35)
	{
		for (track = 36 * 2; track <= end_track; track += 2)
		{
			memset(track_buffer + (track * NIB_TRACK_LENGTH), 0, NIB_TRACK_LENGTH);
			track_density[track] = (2 | BM_NO_SYNC);
			track_length[track] = 0;
		}
	}

	fclose(fpin);
	printf("Successfully loaded D64 file\n");
	return 1;
}

int write_nib(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
    /*	writes contents of buffers into NIB file, with header and density information */

  int track;
  FILE * fpout;
	char header[0x100];
	int header_entry = 0;

	/* clear header */
	memset(header, 0, sizeof(header));

	/* create output file */
	if ((fpout = fopen(filename, "wb")) == NULL)
	{
		fprintf(stderr, "Couldn't create output file %s!\n", filename);
		return 0;
	}

	/* header now contains whether halftracks were read */
	if(track_inc == 1)
		sprintf(header, "MNIB-1541-RAW%c%c%c", 3, 0, 1);
	else
		sprintf(header, "MNIB-1541-RAW%c%c%c", 3, 0, 0);

	if (fwrite(header, sizeof(header), 1, fpout) != 1) {
		printf("unable to write NIB header\n");
		return 0;
	}

	for (track = start_track; track <= end_track; track += track_inc)
	{
		header[0x10 + (header_entry * 2)] = (BYTE)track;
		header[0x10 + (header_entry * 2) + 1] = track_density[track];
		header_entry++;

		/* process and save track to disk */
		if (fwrite(track_buffer + (NIB_TRACK_LENGTH * track), NIB_TRACK_LENGTH , 1, fpout) != 1)
		{
			printf("unable to rewrite NIB track data\n");
			fclose(fpout);
			return 0;
		}
		fflush(fpout);
	}

	/* fill NIB-header */
	rewind(fpout);

	if (fwrite(header, sizeof(header), 1, fpout) != 1) {
		printf("unable to rewrite NIB header\n");
		return 0;
	}

	fclose(fpout);
	printf("Successfully saved NIB file\n");
	return 1;
}

int
write_g64(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
	/* writes contents of buffers into NIB file, with header and density information */
	BYTE header[12];
	DWORD gcr_track_p[MAX_HALFTRACKS_1541];
	DWORD gcr_speed_p[MAX_HALFTRACKS_1541];
	BYTE gcr_track[G64_TRACK_MAXLEN + 2];
	int track;
	FILE * fpout;
	int track_len;
	BYTE buffer[NIB_TRACK_LENGTH];

	fpout = fopen(filename, "wb");
	if (fpout == NULL)
	{
		fprintf(stderr, "Cannot open G64 image %s.\n", filename);
		return 0;
	}

	/* Create G64 header */
	strcpy((char *) header, "GCR-1541");
	header[8] = 0;	/* G64 version */
	header[9] = (BYTE) end_track;	/* Number of Halftracks */
	header[10] = (BYTE) (G64_TRACK_MAXLEN % 256);	/* Size of each stored track */
	header[11] = (BYTE) (G64_TRACK_MAXLEN / 256);

	if (fwrite(header, sizeof(header), 1, fpout) != 1)
	{
		fprintf(stderr, "Cannot write G64 header.\n");
		return 0;
	}

	/* Create index and speed tables */
	for (track = 0; track < (MAX_TRACKS_1541 * 2); track += track_inc)
	{
		/* calculate track positions and speed zone data */
		if(track_inc == 2)
		{
			gcr_track_p[track] = 12 + MAX_TRACKS_1541 * 16 + (track/2) * (G64_TRACK_MAXLEN + 2);
			gcr_track_p[track+1] = 0;	/* no halftracks */

			//if(skip_halftracks)
			//	gcr_speed_p[track] =  track_density[track*2]; //(nib_header[17 + (track * 2)] & 0x03);
			//else
				gcr_speed_p[track] = track_density[track+2]; //(nib_header[17 + track] & 0x03);

			gcr_speed_p[track+1] = 0;
		}
		else
		{
			gcr_track_p[track] = 12 + MAX_TRACKS_1541 * 16 + track * (G64_TRACK_MAXLEN + 2);
			gcr_speed_p[track] = track_density[track+2]; //(nib_header[17 + (track * 2)] & 0x03);
		}

	}

	if (write_dword(fpout, gcr_track_p, sizeof(gcr_track_p)) < 0)
	{
		fprintf(stderr, "Cannot write track header.\n");
		return 0;
	}
	if (write_dword(fpout, gcr_speed_p, sizeof(gcr_speed_p)) < 0)
	{
		fprintf(stderr, "Cannot write speed header.\n");
		return 0;
	}

	/* shuffle raw GCR between formats */
	for (track = 0; track < 82; track += track_inc)
	{
		int raw_track_size[4] = { 6250, 6666, 7142, 7692 };

		memset(&gcr_track[2], 0, G64_TRACK_MAXLEN);

		gcr_track[0] = raw_track_size[speed_map_1541[track/2]] % 256;
		gcr_track[1] = raw_track_size[speed_map_1541[track/2]] / 256;

		/* Skip halftracks if present and directed to do so */
		//if (skip_halftracks)
		//	fseek(fpin, NIB_TRACK_LENGTH, SEEK_CUR);

		align = ALIGN_NONE;

		track_len = extract_GCR_track(buffer, track_buffer + ((track+2) * NIB_TRACK_LENGTH), &align,
			force_align, capacity_min[gcr_speed_p[track]], capacity_max[gcr_speed_p[track]]);

		track_len = process_halftrack(track+2, buffer, track_density[track+2], track_length[track+2]);

		if(track_len == 0)
		{
			/* track doesn't exist: write blank track */
			track_len = raw_track_size[speed_map_1541[track/2]];
			memset(&gcr_track[2], 0, track_len);
		}
		else if (track_len > G64_TRACK_MAXLEN)
		{
			printf("  Warning: track too long, cropping to %d!", G64_TRACK_MAXLEN);
			track_len = G64_TRACK_MAXLEN;
		}
		gcr_track[0] = track_len % 256;
		gcr_track[1] = track_len / 256;

		// copy back our realigned track
		memcpy(gcr_track+2, buffer, track_len);

		if (fwrite(gcr_track, (G64_TRACK_MAXLEN + 2), 1, fpout) != 1)
		{
			fprintf(stderr, "Cannot write track data.\n");
			return 0;
		}
	}
	fclose(fpout);
	printf("\nSuccessfully saved G64 file\n");
	return 1;
}

int
write_d64(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
{
    /*	writes contents of buffers into D64 file, with errorblock information (if detected) */
	FILE *fpout;
	int track, sector;
	int save_errorinfo, save_40_errors, save_40_tracks;
	BYTE id[3];
	BYTE rawdata[260];
	BYTE d64data[MAXBLOCKSONDISK * 256], *d64ptr;
	BYTE errorinfo[MAXBLOCKSONDISK], errorcode;
	int blocks_to_save;

	/* create output file */
	if ((fpout = fopen(filename, "wb")) == NULL)
	{
		fprintf(stderr, "Couldn't create output file %s!\n", filename);
		exit(2);
	}

	save_errorinfo = 0;
	save_40_errors = 0;
	save_40_tracks = 0;

	/* get disk id */
	if (!extract_id(track_buffer + (18 * 2 * NIB_TRACK_LENGTH), id))
	{
		fprintf(stderr, "Cannot find directory sector.\n");
		return 0;
	}

	d64ptr = d64data;
	for (track = start_track; track <= 40*2; track += track_inc)
	{
		printf("%.2d (%d):" ,track/2, track_length[track]);

		for (sector = 0; sector < sector_map_1541[track/2]; sector++)
		{
			printf("%d", sector);

			errorcode = convert_GCR_sector(track_buffer + (track * NIB_TRACK_LENGTH),
			track_buffer + (track * NIB_TRACK_LENGTH) + track_length[track],
				rawdata, track/2, sector, id);

			if (errorcode != SECTOR_OK)
			{
				if (track/2 <= 35)
					save_errorinfo = 1;
				else
					save_40_errors = 1;
			}
			else if (track/2 > 35)
			{
				save_40_tracks = 1;
			}

			/* screen information */
			if (errorcode == SECTOR_OK)
				printf(" ");
			else
				printf("%d", errorcode);

			/* dump to buffer */
			memcpy(d64ptr, rawdata+1 , 256);
			d64ptr += 256;
		}
		printf("\n");
	}

	blocks_to_save = (save_40_tracks) ? MAXBLOCKSONDISK : BLOCKSONDISK;

	if (fwrite(d64data, blocks_to_save * 256, 1, fpout) != 1)
	{
		fprintf(stderr, "Cannot write d64 data.\n");
		return 0;
	}

	if (save_errorinfo == 1)
	{
		assert(sizeof(errorinfo) >= (size_t)blocks_to_save);
		if (fwrite(errorinfo, blocks_to_save, 1, fpout) != 1)
		{
			fprintf(stderr, "Cannot write sector data.\n");
			return 0;
		}
	}
	fclose(fpout);
	printf("Successfully saved D64 file\n");
	return 1;
}

int
write_dword(FILE * fd, DWORD * buf, int num)
{
	int i;
	BYTE *tmpbuf;

	tmpbuf = malloc(num);

	for (i = 0; i < (num / 4); i++)
	{
		tmpbuf[i * 4] = buf[i] & 0xff;
		tmpbuf[i * 4 + 1] = (buf[i] >> 8) & 0xff;
		tmpbuf[i * 4 + 2] = (buf[i] >> 16) & 0xff;
		tmpbuf[i * 4 + 3] = (buf[i] >> 24) & 0xff;
	}

	if (fwrite(tmpbuf, num, 1, fd) < 1)
	{
		free(tmpbuf);
		return -1;
	}
	free(tmpbuf);
	return 0;
}

int
process_halftrack(int halftrack, BYTE *track_buffer, BYTE density, int length)
{
	int orglen;
	int badgcr = 0;
	BYTE gcrdata[NIB_TRACK_LENGTH];

	/* copy to spare buffer */
	memcpy(gcrdata, track_buffer, NIB_TRACK_LENGTH);

	/* double-check our sync-flag assumptions */
	density = (BYTE) check_sync_flags(gcrdata, density, length);

	/* user display */
	printf("\n%4.1f: (", (float) halftrack / 2);
	printf("%d", density & 3);
	if (density != speed_map_1541[(halftrack / 2) - 1]) printf("!");
	printf(":%d) ", length);
	if (density & BM_NO_SYNC) printf("{NOSYNC!}");
	else if (density & BM_FF_TRACK) printf("{KILLER!}");
	printf(" [");

	/* compress / expand track */
	if (length > 0)
	{
		// handle bad GCR / weak bits
		badgcr = check_bad_gcr(gcrdata, length, fix_gcr);
		if (badgcr > 0) printf("weak:%d ", badgcr);

		// If our track contains sync, we reduce to a minimum of 16
		// (only 10 are required, technically)
		orglen = length;
		if (length > (capacity[density & 3] - CAPACITY_MARGIN) && !(density & BM_NO_SYNC) && reduce_syncs)
		{
			// then try to reduce sync within the track
			if (length > capacity[density & 3] - CAPACITY_MARGIN)
				length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0xff);

			if (length < orglen)
				printf("rsync:%d ", orglen - length);
		}

		// We could reduce gap bytes ($55 and $AA) here too,
		orglen = length;
		if (length > (capacity[density & 3] - CAPACITY_MARGIN) && reduce_gaps)
		{
			length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0x55);
			length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0xaa);

			if (length < orglen)
				printf("rgaps:%d ", orglen - length);
		}

		// reduce weak bit runs (experimental)
		orglen = length;
		if (length > (capacity[density & 3] - CAPACITY_MARGIN) && badgcr > 0 && reduce_weak)
		{
			length = reduce_runs(gcrdata, length, capacity[density & 3] - CAPACITY_MARGIN, 2, 0x00);

			if (length < orglen)
				printf("rweak:%d ", orglen - length);
		}

		// still not small enough, we have to truncate the end
		orglen = length;
		if (length > capacity[density & 3] - CAPACITY_MARGIN)
		{
			length = capacity[density & 3] - CAPACITY_MARGIN;

			if (length < orglen)
				printf("trunc:%d ", orglen - length);
			else
				printf("\nHad to truncate track %d by %d bytes.", halftrack / 2, orglen - length);
		}

		// handle short tracks
		orglen = length;
		if(length < capacity[density & 3] - CAPACITY_MARGIN)
		{
			memset(gcrdata + length, 0x55, capacity[density & 3] - CAPACITY_MARGIN - length);
			length = capacity[density & 3] - CAPACITY_MARGIN;
			printf("pad:%d ", length - orglen);
		}
	}

	// if track is empty (unformatted) overfill with '0' bytes to simulate
	if ( (!length) && (density & BM_NO_SYNC))
	{
		memset(gcrdata, 0, NIB_TRACK_LENGTH);
		length = NIB_TRACK_LENGTH;
	}

	// replace 0x00 bytes by 0x01, as 0x00 indicates end of track
	replace_bytes(gcrdata, length, 0x00, 0x01);

	// write processed track buffer
	memcpy(track_buffer, gcrdata, length);

	printf("] (%d) ", length);

	// print out track alignment, as determined
	if (imagetype == IMAGE_NIB)
	{
		switch (align)
		{
			case ALIGN_NONE:
				printf("(none) ");
				break;
			case ALIGN_SEC0:
				printf("(sec0) ");
				break;
			case ALIGN_GAP:
				printf("(gap) ");
				break;
			case ALIGN_LONGSYNC:
				printf("(sync) ");
				break;
			case ALIGN_WEAK:
				printf("(weak) ");
				break;
			case ALIGN_VMAX:
				printf("(v-max) ");
				break;
			case ALIGN_AUTOGAP:
				printf("(auto) ");
				break;
		}
	}
	return length;
}
