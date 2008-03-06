/*
    NIBREPAIR - part of the NIBTOOLS package for 1541/1571 disk image nibbling
	by Peter Rittwage <peter(at)rittwage(dot)com>
    based on code from MNIB, by Dr. Markus Brenner

	This program will repair some errors in G64 images based on common GCR misreads.

	1) tri-bit error, in which 01110 is misinterpreted as 01000
	2) low frequency error, in which 10010 is misinterpreted as 11000

	When these errors cannot be found, we will just change the checksum, making a slightly corrupt image that
	will be innacurate, but may load instead of just failing with a disk error
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"
#include "version.h"

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
int track_length[MAX_HALFTRACKS_1541 + 1];

int start_track, end_track, track_inc;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, align, force_align;
int gap_match_length;
int skip_halftracks;
int verbose = 0;

/* local prototypes */
int repair(void);
BYTE repair_GCR_sector(BYTE *gcr_start, BYTE *gcr_cycle, int track, int sector, BYTE *id);


int ARCH_MAINDECL
main(int argc, char **argv)
{
	char inname[256], outname[256], *dotpos;

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	fix_gcr = 0;
	reduce_sync = 1;
	reduce_badgcr = 0;
	reduce_gap = 0;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;

	fprintf(stdout,
	  "\nnibrepair - converts a damaged NIB/NB2/G64 to a new 'repaired' G64 file.\n"
	  "(C) Pete Rittwage, Dr. Markus Brenner, and friends.\n"
	  "Version " VERSION "\n\n");

	if(!(track_buffer = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate memory for buffers.\n");
		exit(0);
	}

	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{
		case 'f':
			printf("* Fix bad GCR\n");
			fix_gcr = 1;
			break;

		case 'r':
			printf("* Reduce sync disabled\n");
			reduce_sync = 0;
			break;

		case '0':
			printf("* Reduce bad GCR enabled\n");
			reduce_badgcr = 1;
			break;

		case 'g':
			printf("* Reduce gaps enabled\n");
			reduce_gap = 1;
			break;

		case 'G':
			if (!(*argv)[2]) usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'p':
			// custom protection handling
			printf("* Custom copy protection handler: ");
			if ((*argv)[2] == 'x')
			{
				printf("V-MAX!\n");
				force_align = ALIGN_VMAX;
				fix_gcr = 0;
			}
			else if ((*argv)[2] == 'c')
			{
				printf("V-MAX! (CINEMAWARE)\n");
				force_align = ALIGN_VMAX_CW;
				fix_gcr = 0;
			}
			else if ((*argv)[2] == 'g')
			{
				printf("GMA/SecuriSpeed\n");
				reduce_sync = 0;
				reduce_badgcr = 1;
			}
			else if ((*argv)[2] == 'v')
			{
				printf("VORPAL (NEWER)\n");
				force_align = ALIGN_AUTOGAP;
			}
			else if ((*argv)[2] == 'r')
			{
				printf("RAPIDLOK\n");
				reduce_sync = 0;
				reduce_badgcr = 1;
				reduce_gap = 1;
			}
			else
				printf("Unknown protection handler\n");
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
				printf("longest bad gcr run\n");
				force_align = ALIGN_BADGCR;
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

		case 'v':
			printf("* Verbose mode (more detailed track info)\n");
			verbose = 1;
			break;

		default:
			usage();
			break;
		}
	}

	if(argc < 1)	usage();
	strcpy(inname, argv[0]);

	strcpy(outname, inname);
	dotpos = strrchr(outname, '.');
	if (dotpos != NULL) *dotpos = '\0';
	strcat(outname, "_repaired.g64");

	printf("%s -> %s\n",inname, outname);

	/* convert */
	if (compare_extension(inname, "G64"))
	{
		if(!read_g64(inname, track_buffer, track_density, track_length))
			exit(0);
	}
	else if (compare_extension(inname, "NIB"))
	{
		if(!read_nib(inname, track_buffer, track_density, track_length, track_alignment))
			exit(0);
	}
	else if (compare_extension(inname, "NB2"))
	{
		if(!read_nb2(inname, track_buffer, track_density, track_length, track_alignment))
			exit(0);
	}
	else
	{
		printf("Unknown input file type\n");
		exit(0);
	}

	if(skip_halftracks) track_inc = 2;

	repair();

	write_g64(outname, track_buffer, track_density, track_length);
	return 0;
}

int repair(void)
{
	int track, sector;
	int blockindex = 0;
	BYTE id[3];
	BYTE errorinfo[MAXBLOCKSONDISK], errorcode;

	printf("\nScanning for errors...\n");

	/* get disk id */
	if (!extract_id(track_buffer + (18 * 2 * NIB_TRACK_LENGTH), id))
	{
		fprintf(stderr, "Cannot find directory sector.\n");
		return 0;
	}

	for (track = start_track; track <= 35*2 /*end_track*/; track += track_inc)
	{
		for (sector = 0; sector < sector_map_1541[track/2]; sector++)
		{
				errorcode = repair_GCR_sector(track_buffer + (track * NIB_TRACK_LENGTH),
																		track_buffer + (track * NIB_TRACK_LENGTH) + track_length[track],
																		track/2, sector, id);

				errorinfo[blockindex] = errorcode;

				switch(errorcode)
				{
						case SYNC_NOT_FOUND:
								printf("T%dS%d Sync not found - Cannot repair\n", track/2, sector);
								break;

						case HEADER_NOT_FOUND:
								printf("T%dS%d Header not found - Cannot repair\n", track/2, sector);
								break;

						case DATA_NOT_FOUND:
								printf("T%dS%d Data block not found - Cannot repair\n", track/2, sector);
								break;

						case ID_MISMATCH:
								printf("T%dS%d Disk ID Mismatch - Cannot repair\n", track/2, sector);
								break;

						case BAD_GCR_CODE:
								printf("T%dS%d Illegal GCR - Cannot repair\n", track/2, sector);
								break;

				}
				blockindex++;
		}
	}

	return 0;
}

BYTE repair_GCR_sector(BYTE *gcr_start, BYTE *gcr_cycle, int track, int sector, BYTE *id)
{

	/* Try to repair some common GCR errors
			1) tri-bit error, in which 01110 is misinterpreted as 01000
			2) low frequency error, in which 10010 is misinterpreted as 11000

		Failing that, we just fix the checksums, which creates an innaccurate image, but maybe it will load!

	*/

	BYTE header[10];	/* block header */
	BYTE hdr_chksum;	/* header checksum */
	BYTE blk_chksum;	/* block  checksum */
	BYTE *gcr_ptr, *gcr_end, *gcr_last;
	BYTE *sectordata;
	BYTE error_code;
    int sync_found, i, j;
    size_t track_len;
    BYTE d64_sector[260];
    BYTE answer;

	error_code = SECTOR_OK;

	if (track > MAX_TRACK_D64)
	{
		//printf("no valid sectors above 40 tracks\n");
		return SYNC_NOT_FOUND;
	}

	if (gcr_cycle == NULL || gcr_cycle <= gcr_start)
	{
		/* printf("no track cycle, no data\n"); */
		return SYNC_NOT_FOUND;
	}

	/* initialize sector data with Original Format Pattern */
	memset(d64_sector, 0x01, 260);
	d64_sector[0] = 0x07;	/* Block header mark */
	d64_sector[1] = 0x4b;	/* Use Original Format Pattern */

	for (blk_chksum = 0, i = 1; i < 257; i++)
		blk_chksum ^= d64_sector[i + 1];

	d64_sector[257] = blk_chksum;

	/* copy to  temp. buffer with twice the track data */
	track_len = gcr_cycle - gcr_start;

	/* Check for at least one Sync */
	gcr_end = gcr_start + track_len;
	sync_found = 0;

	for (gcr_ptr = gcr_start; gcr_ptr < gcr_end; gcr_ptr++)
	{
		if (*gcr_ptr == 0xff)
		{
			if (sync_found < 2)
				sync_found++;
		}
		else		/* (*gcr_ptr != 0xff) */
		{
			if (sync_found < 2)
				sync_found = 0;
			else
				sync_found = 3;
		}
	}
	if (sync_found != 3)
		return (SYNC_NOT_FOUND);

	/* Check for missing SYNCs */
	gcr_last = gcr_ptr;;
	while (gcr_ptr < gcr_end)
	{
		find_sync(&gcr_ptr, gcr_end);
		if ((gcr_ptr - gcr_last) > MAX_SYNC_OFFSET)
		{
			//printf("no sync for %d\n", gcr_ptr-gcr_last);
			return (SYNC_NOT_FOUND);
		}
		else
			gcr_last = gcr_ptr;
	}

	/* Try to find a good block header for Track/Sector */
	gcr_ptr = gcr_start;
	gcr_end = gcr_start + track_len;

	do
	{
		if (!find_sync(&gcr_ptr, gcr_end) || gcr_ptr >= gcr_end - 10)
		{
			error_code = HEADER_NOT_FOUND;
			break;
		}
		convert_4bytes_from_GCR(gcr_ptr, header);
		convert_4bytes_from_GCR(gcr_ptr + 5, header + 4);
		gcr_ptr++;

	} while (header[0] != 0x08 || header[2] != sector ||
	  header[3] != track || header[5] != id[0] || header[4] != id[1]);

	if (error_code == HEADER_NOT_FOUND)
	{
		error_code = SECTOR_OK;
		/* Try to find next best match for header */
		gcr_ptr = gcr_start;
		gcr_end = gcr_start + track_len;
		do
		{
			if (!find_sync(&gcr_ptr, gcr_end))
				return (HEADER_NOT_FOUND);

			if (gcr_ptr >= gcr_end - 10)
				return (HEADER_NOT_FOUND);

			convert_4bytes_from_GCR(gcr_ptr, header);
			convert_4bytes_from_GCR(gcr_ptr + 5, header + 4);
			gcr_ptr++;

		} while (header[0] == 0x07 || header[2] != sector || header[3] != track);
	}

	if (header[0] != 0x08)
		error_code = (error_code == SECTOR_OK) ? HEADER_NOT_FOUND : error_code;

	/* Header checksum */
	hdr_chksum = 0;
	for (i = 1; i <= 4; i++)
		hdr_chksum = hdr_chksum ^ header[i];

	if (hdr_chksum != header[5])
	{
		printf("T%dS%d Bad Header Checksum $%.2x != $%.2x - Repair (Y/n)? ", track, sector, hdr_chksum, header[5]);
		fflush(stdin);
		answer = getchar();

		if(answer != 'n')
		{
			/* patch back */
			header[5] = hdr_chksum;
			gcr_ptr--;
			convert_4bytes_from_GCR(header, gcr_ptr);
			convert_4bytes_from_GCR(header + 4, gcr_ptr + 5);
			gcr_ptr++;
			printf("Repaired\n");
		}
		else
			printf("Not repaired\n");


		error_code = (error_code == SECTOR_OK) ? BAD_HEADER_CHECKSUM : error_code;
	}

	/* verify that our header contains no bad GCR, since it can be false positive checksum match */
	for(j = 0; j < 10; j++)
	{
		if (is_bad_gcr(gcr_ptr - 1, 10, j)) error_code = (error_code == SECTOR_OK) ? BAD_GCR_CODE : error_code;
	}

	/* check for data sector */
	if (!find_sync(&gcr_ptr, gcr_end))
		return (DATA_NOT_FOUND);

	for (i = 0, sectordata = d64_sector; i < 65; i++)
	{
		if (gcr_ptr >= gcr_end - 5)
			return (DATA_NOT_FOUND);  /* short sector */

		convert_4bytes_from_GCR(gcr_ptr, sectordata);
		gcr_ptr += 5;
		sectordata += 4;
	}

	/* check for correct disk ID */
	if (header[5] != id[0] || header[4] != id[1])
		error_code = (error_code == SECTOR_OK) ? ID_MISMATCH : error_code;

	/* check for Block header mark */
	if (d64_sector[0] != 0x07)
		error_code = (error_code == SECTOR_OK) ? DATA_NOT_FOUND : error_code;

	/* Block checksum */
	for (i = 1, blk_chksum = 0; i <= 256; i++)
		blk_chksum ^= d64_sector[i];

	if (blk_chksum != d64_sector[257])
	{
		printf("T%dS%d Bad Data Checksum $%.2x != $%.2x - Repair (Y/n)? ", track, sector, blk_chksum, d64_sector[257]);
		fflush(stdin);
		answer = getchar();

		if(answer != 'n')
		{
			/* patch back */
			d64_sector[257] = blk_chksum;
			gcr_ptr -= 330;
			for (i = 0, sectordata = d64_sector; i < 65; i++)
			{
				convert_4bytes_to_GCR(sectordata, gcr_ptr);
				gcr_ptr += 5;
				sectordata += 4;
			}
			printf("Repaired\n");
		}
		else
			printf("Not repaired\n");

		error_code = (error_code == SECTOR_OK) ? BAD_DATA_CHECKSUM : error_code;
	}

	/* verify that our data contains no bad GCR, since it can be false positive checksum match */
	for(j = 0; j < 320; j++)
		if (is_bad_gcr(gcr_ptr - 325, 320, j)) error_code = (error_code == SECTOR_OK) ? BAD_GCR_CODE : error_code;

	return (error_code);
}

void
usage(void)
{
	fprintf(stderr,
	"usage: nibrepair [options] <infile>\n"
	"\nsupported file types for repair:\n"
	"NIB, NB2, G64\n"
	"\noptions:\n"
	" -a[x]: Force alternative track alignments (advanced users only)\n"
	" -p[x]: Custom protection handlers (advanced users only)\n"
     " -g: Enable gap reduction\n"
     " -0: Enable bad GCR run reduction\n"
     " -r: Enable automatic sync reduction\n"
     " -G: Manual gap match length\n");
	exit(1);
}
