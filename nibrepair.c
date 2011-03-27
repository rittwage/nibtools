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
#include "lz.h"

int _dowildcard = 1;

BYTE compressed_buffer[(MAX_HALFTRACKS_1541+2) * NIB_TRACK_LENGTH];
BYTE file_buffer[(MAX_HALFTRACKS_1541+2) * NIB_TRACK_LENGTH];
BYTE track_buffer[(MAX_HALFTRACKS_1541+1) * NIB_TRACK_LENGTH];
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
size_t track_length[MAX_HALFTRACKS_1541 + 1];
int file_buffer_size;
int start_track, end_track, track_inc;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, align, force_align;
int gap_match_length;
int cap_min_ignore;
int skip_halftracks;
int verbose = 0;
int rpm_real;
int ihs;
int auto_capacity_adjust;
int align_disk;
int skew;
int mode;
int unformat_passes;
int capacity_margin;
int align_delay;
int increase_sync = 0;
int presync = 0;
BYTE fillbyte = 0x55;
BYTE drive = 8;
char * cbm_adapter = "";

/* local prototypes */
int repair(void);
BYTE repair_GCR_sector(BYTE *gcr_start, BYTE *gcr_cycle, int track, int sector, BYTE *id);

int ARCH_MAINDECL
main(int argc, char **argv)
{
	char inname[256], outname[256];
	char *dotpos;

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	fix_gcr = 1;
	reduce_sync = 3;
	reduce_badgcr = 0;
	reduce_gap = 0;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;
	cap_min_ignore = 0;
	rpm_real = 0;

	fprintf(stdout,
	  "\nnibrepair - converts a damaged NIB/NB2/G64 to a new 'repaired' G64 file.\n"
	  "(C) 2004-2010 Peter Rittwage\nC64 Preservation Project\nhttp://c64preservation.com\n"
	  "Revision %d - " VERSION "\n\n", SVN);

	/* clear heap buffers */
	memset(compressed_buffer, 0x00, sizeof(compressed_buffer));
	memset(file_buffer, 0x00, sizeof(file_buffer));
	memset(track_buffer, 0x00, sizeof(track_buffer));

	/* default is to reduce sync */
	memset(reduce_map, REDUCE_SYNC, MAX_TRACKS_1541+1);

	while (--argc && (*(++argv)[0] == '-'))
		parseargs(argv);

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
		if(!(read_g64(inname, track_buffer, track_density, track_length))) exit(0);
	}
	else if (compare_extension(inname, "NBZ"))
	{
		printf("Uncompressing NBZ...\n");
		if(!(file_buffer_size = load_file(inname, compressed_buffer))) exit(0);
		if(!(file_buffer_size = LZ_Uncompress(compressed_buffer, file_buffer, file_buffer_size))) exit(0);
		if(!(read_nib(file_buffer, file_buffer_size, track_buffer, track_density, track_length))) exit(0);
		align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(inname, "NIB"))
	{
		if(!(file_buffer_size = load_file(inname, file_buffer))) exit(0);
		if(!(read_nib(file_buffer, file_buffer_size, track_buffer, track_density, track_length))) exit(0);
		align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(inname, "NB2"))
	{
		if(!(read_nb2(inname, track_buffer, track_density, track_length))) exit(0);
		align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(inname, "D64"))
	{
		if(!(read_d64(inname, track_buffer, track_density, track_length))) exit(0);
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
		for (sector = 0; sector < sector_map[track/2]; sector++)
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
    int i, j;
    size_t track_len;
    BYTE d64_sector[260];
    int answer;

	error_code = SECTOR_OK;
	track_len = gcr_cycle - gcr_start;

	if ((track > MAX_TRACK_D64) || (!track_len))
		return SYNC_NOT_FOUND;

	/* initialize sector data with Original Format Pattern */
	memset(d64_sector, 0x01, 260);
	d64_sector[0] = 0x07;	/* Block header mark */
	d64_sector[1] = 0x4b;	/* Use Original Format Pattern */
	for (blk_chksum = 0, i = 1; i < 257; i++)
		blk_chksum ^= d64_sector[i + 1];
	d64_sector[257] = blk_chksum;

	/* Check for missing SYNCs */
  	gcr_last = gcr_ptr = gcr_start;
  	gcr_end = gcr_cycle;
    while (gcr_ptr < gcr_end)
    {
        find_sync(&gcr_ptr, gcr_end);
        if ((gcr_ptr-gcr_last) > MAX_SYNC_OFFSET)
            return (SYNC_NOT_FOUND);
        else
            gcr_last = gcr_ptr;
   }

	/* Try to find block header for Track/Sector */
    gcr_ptr = gcr_start;
    do
    {
        if (!find_sync(&gcr_ptr, gcr_end)) return (HEADER_NOT_FOUND);
        if (gcr_ptr >= gcr_end - 10) return (HEADER_NOT_FOUND);
        convert_4bytes_from_GCR(gcr_ptr, header);
        convert_4bytes_from_GCR(gcr_ptr+5, header+4);
        gcr_ptr++;
    } while ((header[0]==0x07) || (header[2]!=sector) || (header[3]!=track));

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
		if (is_bad_gcr(gcr_ptr - 1, 10, j)) error_code = (error_code == SECTOR_OK) ? BAD_GCR_CODE : error_code;

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
			gcr_ptr -= 325;
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
	fprintf(stderr, "usage: nibrepair [options] <filename>\n\n");
	switchusage();
	exit(1);
}
