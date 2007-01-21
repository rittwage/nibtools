/*
    nibconv - Converts mnib nibbler data to G64 image

    (C) 2000-03 Markus Brenner <markus(at)brenner(dot)de>
		and Pete Rittwage <peter(at)rittwage(dot)com>

    Based on code by Andreas Boose <boose(at)linux(dot)rz(dot)fh-hannover(dot)de>

    V 0.21   use correct speed values in G64
    V 0.22   cleaned up version using gcr.c helper functions
    V 0.23   ignore halftrack information if present
    V 0.24   fixed density information
    V 0.35   moved extract_GCR_track() to gcr.c, unified versioning

    V 0.36   updated many options to match upgrades in mnib
             fixed halftrack handling (skipping), empty track skipping,
             weak-bit ($00 bytes) support for emulators, track
             reduction needed for tracks longer than 7928 bytes, and
             access to the new custom protection handlers. (prittwage)

	V 0.45.1 added ability to change maximum G64 track size from the
				command line, and fixed buffer overruns and readability (prittwage)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include "mnibarch.h"
#include "gcr.h"
#include "version.h"

int gap_match_length;

static int
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

static void
usage(void)
{
	fprintf(stderr, "usage: nibconv [options] <infile> [outfile]\n");
	exit(1);
}

int ARCH_MAINDECL
main(int argc, char **argv)
{
	FILE *fpin, *fpout;
	char inname[256], outname[256], *dotpos;
	BYTE gcr_header[12];
	DWORD gcr_track_p[MAX_HALFTRACKS_1541];
	DWORD gcr_speed_p[MAX_HALFTRACKS_1541];
	DWORD track_len;
	BYTE mnib_track[NIB_TRACK_LENGTH];
	BYTE tmp_mnib_track[NIB_TRACK_LENGTH];
	BYTE *gcr_track;
	BYTE nib_header[0x100];
	int track, badgcr, fixgcr;
	int align, force_align, org_len;
	int reduce_syncs, reduce_weak, reduce_gaps;
	int start_track, end_track, track_inc, numtracks, nibsize;
	int skip_halftracks;
	DWORD g64_max_track_size;

	fixgcr = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	reduce_syncs = 0;
	reduce_weak = 0;
	reduce_gaps = 0;
	gap_match_length = 7;
	start_track = 2;
	end_track = 84;
	skip_halftracks = 0;
	g64_max_track_size = G64_TRACK_MAXLEN;

	fprintf(stdout,
	  "\nnibconv - converts a NIB type disk dump into an emulator-compatible image.\n"
	  "(C) Markus Brenner and Pete Rittwage.\n"
	  "Version " VERSION "\n\n");

	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{
		case 'f':
			printf("* Fix weak GCR\n");
			fixgcr = 1;
			break;

		case 'r':
			printf("* Reduce syncs enabled\n");
			reduce_syncs = 1;
			break;

		case '0':
			printf("* Reduce weak GCR enabled\n");
			reduce_weak = 1;
			break;

		case 'g':
			printf("* Reduce gaps enabled\n");
			reduce_gaps = 1;
			break;

		case 'G':
			if (!(*argv)[2])
				usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 't':
			if (!(*argv)[2])
				usage();
			g64_max_track_size = atoi((char *) (&(*argv)[2]));
			printf("* Maximum track size set to %d\n", g64_max_track_size);
			break;

		case 'a':
			// custom alignment handling
			printf("ARG: Custom alignment = ");
			if ((*argv)[2] == '0')
			{
				printf("sector 0\n");
				force_align = ALIGN_SEC0;
			}
			else if ((*argv)[2] == 'w')
			{
				printf("longest weak run\n");
				force_align = ALIGN_WEAK;
			}
			else if ((*argv)[2] == 's')
			{
				printf("longest sync\n");
				force_align = ALIGN_LONGSYNC;
			}
			else if ((*argv)[2] == 'a')
			{
				printf("autogap\n");
				force_align = ALIGN_AUTOGAP;
			}
			else
				printf("Unknown alignment parameter\n");
			break;

		case 'h':
			printf("* Skipping halftracks\n");
			skip_halftracks = 1;
			break;

		default:
			usage();
			break;
		}
	}

	if (argc == 1)
	{
		strcpy(inname, argv[0]);
		strcpy(outname, inname);
	}
	else if (argc == 2)
	{
		strcpy(inname, argv[0]);
		strcpy(outname, argv[1]);
	}
	else
		usage();

	/* alloc gcr track buffer */
	gcr_track = malloc(g64_max_track_size + 2);
	if(!gcr_track) goto fail;

	dotpos = strrchr(outname, '.');
	if (dotpos != NULL)
		*dotpos = '\0';
	strcat(outname, ".g64");

	fpin = fpout = NULL;

	fpin = fopen(inname, "rb");
	if (fpin == NULL)
	{
		fprintf(stderr, "Cannot open mnib image %s.\n", inname);
		goto fail;
	}

	/* Determine number of tracks in image (crude) */
	fseek(fpin, 0, SEEK_END);
	nibsize = ftell(fpin);
	numtracks = (nibsize - NIB_HEADER_SIZE) / NIB_TRACK_LENGTH;

	if(numtracks <= 42)
	{
		if(skip_halftracks)
		{
			printf("No halftracks found to skip\n");
			skip_halftracks = 0;
		}

		//end_track = (numtracks * 2) + 1;
		track_inc = 2;
	}
	else
	{
		if(skip_halftracks)
				track_inc = 2;
		else
		{
			printf("Image contains halftracks!\n");
			//end_track = numtracks + 1;
			track_inc = 1;
		}
	}

	printf("Total track in NIB file: %d (filesize = %d bytes)\n", numtracks, nibsize);
	printf("Maximum G64 track size: %d bytes\n", g64_max_track_size);

	rewind(fpin);

	if (fread(nib_header, sizeof(nib_header), 1, fpin) < 1)
	{
		fprintf(stderr, "Cannot read header from mnib image.\n");
		goto fail;
	}

	if (memcmp(nib_header, "MNIB-1541-RAW", 13) != 0)
	{
		fprintf(stderr, "input file %s isn't an mnib data file!\n",
		  inname);
		goto fail;
	}

	fpout = fopen(outname, "wb");
	if (fpout == NULL)
	{
		fprintf(stderr, "Cannot open G64 image %s.\n", outname);
		goto fail;
	}

	/* Create G64 header */
	strcpy((char *) gcr_header, "GCR-1541");
	gcr_header[8] = 0;	/* G64 version */
	gcr_header[9] = (BYTE) end_track;	/* Number of Halftracks */
	gcr_header[10] = (BYTE) (g64_max_track_size % 256);	/* Size of each stored track */
	gcr_header[11] = (BYTE) (g64_max_track_size / 256);

	if (fwrite(gcr_header, sizeof(gcr_header), 1, fpout) != 1)
	{
		fprintf(stderr, "Cannot write G64 header.\n");
		goto fail;
	}

	/* Create index and speed tables */
	for (track = 0; track < (MAX_TRACKS_1541 * 2); track += track_inc)
	{
		/* calculate track positions and speed zone data */
		if(track_inc == 2)
		{
			gcr_track_p[track] = 12 + MAX_TRACKS_1541 * 16 + (track/2) * (g64_max_track_size + 2);
			gcr_track_p[track+1] = 0;	/* no halftracks */

			if(skip_halftracks)
				gcr_speed_p[track] = (nib_header[17 + (track * 2)] & 0x03);
			else
				gcr_speed_p[track] = (nib_header[17 + track] & 0x03);

			gcr_speed_p[track+1] = 0;
		}
		else
		{
			gcr_track_p[track] = 12 + MAX_TRACKS_1541 * 16 + track * (g64_max_track_size+2);
			gcr_speed_p[track] = (nib_header[17 + (track * 2)] & 0x03);
		}

	}

	if (write_dword(fpout, gcr_track_p, sizeof(gcr_track_p)) < 0)
	{
		fprintf(stderr, "Cannot write track header.\n");
		goto fail;
	}
	if (write_dword(fpout, gcr_speed_p, sizeof(gcr_speed_p)) < 0)
	{
		fprintf(stderr, "Cannot write speed header.\n");
		goto fail;
	}

	/* shuffle raw GCR between formats */
	for (track = 0; track < 84; track += track_inc)
	{
		int raw_track_size[4] = { 6250, 6666, 7142, 7692 };

		memset(&gcr_track[2], 0, g64_max_track_size);
		memset(tmp_mnib_track, 0, NIB_TRACK_LENGTH);
		memset(mnib_track, 0, NIB_TRACK_LENGTH);

		gcr_track[0] = raw_track_size[speed_map_1541[track/2]] % 256;
		gcr_track[1] = raw_track_size[speed_map_1541[track/2]] / 256;

		/* Skip halftracks if present and directed to do so */
		if (skip_halftracks)
			fseek(fpin, NIB_TRACK_LENGTH, SEEK_CUR);

		/* read in one track */
		if (fread(mnib_track, NIB_TRACK_LENGTH, 1, fpin) < 1)
		{
			/* track doesn't exist: write blank track */
			printf("\nTrack: %.1f: no data ", (float)(track+2)/2);
			track_len = raw_track_size[speed_map_1541[track/2]];
			memset(&gcr_track[2], 0, track_len);
			gcr_track[0] = track_len % 256;
			gcr_track[1] = track_len / 256;
			if (fwrite(gcr_track, (g64_max_track_size + 2), 1, fpout) != 1)
			{
				fprintf(stderr, "Cannot write track data.\n");
				goto fail;
			}
			continue;
		}

		printf("\nTrack: %.1f (%d) ", (float) (track+2)/2, gcr_speed_p[track]);
		align = ALIGN_NONE;

		track_len = extract_GCR_track(tmp_mnib_track, mnib_track, &align,
			force_align, capacity_min[gcr_speed_p[track]],
			capacity_max[gcr_speed_p[track]]);

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

		if (track_len > 0)
		{
			// handle illegal GCR
			badgcr = check_bad_gcr(tmp_mnib_track, track_len, fixgcr);
			if (badgcr) printf("- weak: %d ", badgcr);

			// reduce syncs
			if (reduce_syncs)
			{
				org_len = track_len;
				if (track_len > g64_max_track_size)
				{
					track_len = reduce_runs(tmp_mnib_track, track_len, 7928, 2, 0xff);
					printf("rsync:%d ", org_len - track_len);
				}
			}

			// reduce gaps (experimental)
			if (reduce_gaps)
			{
				org_len = track_len;
				if (track_len > g64_max_track_size)
				{
					// XXX Also can use 0xAA instead of 0x55
					track_len = reduce_runs(tmp_mnib_track, track_len, 7928, 2, 0x55);
					printf("rgaps:%d ", org_len - track_len);
				}
			}

			// reduce weak bit runs (experimental)
			if (reduce_weak)
			{
				org_len = track_len;
				if (track_len > g64_max_track_size)
				{
					track_len = reduce_runs(tmp_mnib_track, track_len, 7928, 2, 0x00);
					printf("rweak:%d ", org_len - track_len);
				}
			}
		}
		printf("- track length: %d ", track_len);

		if (track_len == 0)
		{
			track_len = raw_track_size[speed_map_1541[track/2]];
			memset(&gcr_track[2], 0, track_len);
		}
		else if (track_len > g64_max_track_size)
		{
			printf("  Warning: track too long, cropping to %d!", g64_max_track_size);
			track_len = g64_max_track_size;
		}
		gcr_track[0] = track_len % 256;
		gcr_track[1] = track_len / 256;

		// copy back our realigned track
		memcpy(gcr_track+2, tmp_mnib_track, track_len);

		if (fwrite(gcr_track, (g64_max_track_size + 2), 1, fpout) != 1)
		{
			fprintf(stderr, "Cannot write track data.\n");
			goto fail;
		}
	}

fail:
	if (fpin != NULL)
		fclose(fpin);
	if (fpout != NULL)
		fclose(fpout);
	if(gcr_track != NULL)
		free(gcr_track);
	return (-1);
}
