/*
    gcr.c - Group Code Recording helper functions

    (C) 2001-2005 Markus Brenner <markus(at)brenner(dot)de>
    	and Pete Rittwage <peter(at)rittwage(dot)com>
        based on code by Andreas Boose

    V 0.30   created file based on n2d
    V 0.31   improved error handling of convert_GCR_sector()
    V 0.32   removed some functions, added sector-2-GCR conversion
    V 0.33   improved sector extraction, added find_track_cycle() function
    V 0.34   added MAX_SYNC_OFFSET constant, for better error conversion
    V 0.35   improved find_track_cycle() function
    V 0.36   added bad GCR code detection
    V 0.36b  improved find_sync(), added find_sector_gap(), find_sector0()
    V 0.36c  convert_GCR_sector: search good header before using a damaged one
             improved find_track_cycle(), return max len if no cycle found
    V 0.36d  most additions/fixes referenced in mnib.c (pjr)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcr.h"
#include "prot.h"
#include "crc.h"

BYTE sector_map[MAX_TRACKS_1541 + 1] = {
	0,
	21, 21, 21, 21, 21, 21, 21, 21, 21, 21,	/*  1 - 10 */
	21, 21, 21, 21, 21, 21, 21, 19, 19, 19,	/* 11 - 20 */
	19, 19, 19, 19, 18, 18, 18, 18, 18, 18,	/* 21 - 30 */
	17, 17, 17, 17, 17,						/* 31 - 35 */
	17, 17, 17, 17, 17, 17, 17				/* 36 - 42 (non-standard) */
};

// 7, 17, 12, 8
BYTE sector_gap_length[MAX_TRACKS_1541 + 1] = {
	0,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10,	/*  1 - 10 */
	10, 10, 10, 10, 10, 10, 10, 17, 17, 17,	/* 11 - 20 */
	17, 17, 17, 17, 11, 11, 11, 11, 11, 11,	/* 21 - 30 */
	8, 8, 8, 8, 8,							/* 31 - 35 */
	8, 8, 8, 8, 8, 8, 8						/* 36 - 42 (non-standard) */
};


BYTE speed_map[MAX_TRACKS_1541 + 1] = {
	0,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3,	/*  1 - 10 */
	3, 3, 3, 3, 3, 3, 3, 2, 2, 2,	/* 11 - 20 */
	2, 2, 2, 2, 1, 1, 1, 1, 1, 1,	/* 21 - 30 */
	0, 0, 0, 0, 0,					/* 31 - 35 */
	0, 0, 0, 0, 0, 0, 0				/* 36 - 42 (non-standard) */
};

BYTE align_map[MAX_TRACKS_1541 + 1] = {
	0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/*  1 - 10 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 11 - 20 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 21 - 30 */
	0, 0, 0, 0, 0, 0,				/* 31 - 35 */
	0, 0, 0, 0, 0, 0				/* 37 - 42  */
};

BYTE reduce_map[MAX_TRACKS_1541 + 1] = {
	0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/*  1 - 10 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 11 - 20 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 21 - 30 */
	0, 0, 0, 0, 0, 0,				/* 31 - 35 */
	0,	0, 0, 0, 0, 0				/* 37 - 42  */
};

char alignments[][20] = { "NONE", "GAP", "SEC0", "SYNC", "BADGCR", "VMAX", "AUTO", "VMAX-CW", "RAW", "PIRATESLAYER", "RAPIDLOK"};

/* Burst Nibbler defaults
size_t capacity_min[] = 		{ 6183, 6598, 7073, 7616 };
size_t capacity[] = 			{ 6231, 6646, 7121, 7664 };
size_t capacity_max[] = 		{ 6311, 6726, 7201, 7824 };
*/

/* New calculated defaults */
size_t capacity_min[] =	{ (int) (DENSITY0 / 305), (int) (DENSITY1 / 305), (int) (DENSITY2 / 305), (int) (DENSITY3 / 305) };
size_t capacity[] = 	{ (int) (DENSITY0 / 300), (int) (DENSITY1 / 300), (int) (DENSITY2 / 300), (int) (DENSITY3 / 300) };
size_t capacity_max[] =	{ (int) (DENSITY0 / 295), (int) (DENSITY1 / 295), (int) (DENSITY2 / 295), (int) (DENSITY3 / 295) };

/* Nibble-to-GCR conversion table */
static BYTE GCR_conv_data[16] = {
	0x0a, 0x0b, 0x12, 0x13,
	0x0e, 0x0f, 0x16, 0x17,
	0x09, 0x19, 0x1a, 0x1b,
	0x0d, 0x1d, 0x1e, 0x15
};

/* GCR-to-Nibble conversion tables */
static BYTE GCR_decode_high[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x80, 0x00, 0x10, 0xff, 0xc0, 0x40, 0x50,
	0xff, 0xff, 0x20, 0x30, 0xff, 0xf0, 0x60, 0x70,
	0xff, 0x90, 0xa0, 0xb0, 0xff, 0xd0, 0xe0, 0xff
};

static BYTE GCR_decode_low[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x08, 0x00, 0x01, 0xff, 0x0c, 0x04, 0x05,
	0xff, 0xff, 0x02, 0x03, 0xff, 0x0f, 0x06, 0x07,
	0xff, 0x09, 0x0a, 0x0b, 0xff, 0x0d, 0x0e, 0xff
};


int
find_sync(BYTE ** gcr_pptr, BYTE * gcr_end)
{
	while (1)
	{
		if ((*gcr_pptr) + 1 >= gcr_end)
		{
			*gcr_pptr = gcr_end;
			return 0;	/* not found */
		}

		/* sync flag goes up after the 10th bit */
		//if ( ((*gcr_pptr)[0] & 0x03) == 0x03 && (*gcr_pptr)[1] == 0xff)

		/* but sometimes they are short a bit */
		if ( ((*gcr_pptr)[0] & 0x01) == 0x01 && (*gcr_pptr)[1] == 0xff)
			break;

		(*gcr_pptr)++;
	}

	(*gcr_pptr)++;

	while (*gcr_pptr < gcr_end && **gcr_pptr == 0xff)
		(*gcr_pptr)++;

	return (*gcr_pptr < gcr_end);
}

int
find_header(BYTE ** gcr_pptr, BYTE * gcr_end)
{
	while (1)
	{
		if ((*gcr_pptr) + 2 >= gcr_end)
		{
			*gcr_pptr = gcr_end;
			return 0;	/* not found */
		}

		/* hardware sync flag goes up after the 10th bit */
		//if ( (((*gcr_pptr)[0] & 0x03) == 0x03) && ((*gcr_pptr)[1] == 0xff) && ((*gcr_pptr)[2] == 0x52) )

		/* but sometimes they are short a bit */
		if ( (((*gcr_pptr)[0] & 0x01) == 0x01) && ((*gcr_pptr)[1] == 0xff) && ((*gcr_pptr)[2] == 0x52) )
			break;

		(*gcr_pptr)++;
	}

	(*gcr_pptr)++;

	return (*gcr_pptr < gcr_end);
}

void
convert_4bytes_to_GCR(BYTE * buffer, BYTE * ptr)
{
	*ptr = GCR_conv_data[(*buffer) >> 4] << 3;
	*ptr |= GCR_conv_data[(*buffer) & 0x0f] >> 2;
	ptr++;

	*ptr = GCR_conv_data[(*buffer) & 0x0f] << 6;
	buffer++;
	*ptr |= GCR_conv_data[(*buffer) >> 4] << 1;
	*ptr |= GCR_conv_data[(*buffer) & 0x0f] >> 4;
	ptr++;

	*ptr = GCR_conv_data[(*buffer) & 0x0f] << 4;
	buffer++;
	*ptr |= GCR_conv_data[(*buffer) >> 4] >> 1;
	ptr++;

	*ptr = GCR_conv_data[(*buffer) >> 4] << 7;
	*ptr |= GCR_conv_data[(*buffer) & 0x0f] << 2;
	buffer++;
	*ptr |= GCR_conv_data[(*buffer) >> 4] >> 3;
	ptr++;

	*ptr = GCR_conv_data[(*buffer) >> 4] << 5;
	*ptr |= GCR_conv_data[(*buffer) & 0x0f];
}

int
convert_4bytes_from_GCR(BYTE * gcr, BYTE * plain)
{
	BYTE hnibble, lnibble;
	int badGCR, nConverted;

	badGCR = 0;

	hnibble = GCR_decode_high[gcr[0] >> 3];
	lnibble = GCR_decode_low[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 1;
	*plain++ = hnibble | lnibble;

	hnibble = GCR_decode_high[(gcr[1] >> 1) & 0x1f];
	lnibble = GCR_decode_low[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 2;
	*plain++ = hnibble | lnibble;

	hnibble = GCR_decode_high[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f];
	lnibble = GCR_decode_low[(gcr[3] >> 2) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 3;
	*plain++ = hnibble | lnibble;

	hnibble = GCR_decode_high[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f];
	lnibble = GCR_decode_low[gcr[4] & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 4;
	*plain++ = hnibble | lnibble;

	nConverted = (badGCR == 0) ? 4 : (badGCR - 1);

	return (nConverted);
}

int
extract_id(BYTE * gcr_track, BYTE * id)
{
	BYTE header[10];
	BYTE *gcr_ptr, *gcr_end;
	int track, sector;

	track = 18;
	sector = 0;
	gcr_ptr = gcr_track;
	gcr_end = gcr_track + NIB_TRACK_LENGTH;

	do
	{
		if (!find_sync(&gcr_ptr, gcr_end))
			return 0;

		convert_4bytes_from_GCR(gcr_ptr, header);
		convert_4bytes_from_GCR(gcr_ptr + 5, header + 4);

		if (header[0] == 0x08 && header[2] == 0)
		   id[0] = header[3];

	} while (header[0] != 0x08 || header[2] != sector || header[3] != track);

	id[0] = header[5];
	id[1] = header[4];
	return 1;
}

int
extract_cosmetic_id(BYTE * gcr_track, BYTE * id)
{
	BYTE secbuf[260];
	BYTE error;

	/* get sector into buffer- we don't care about id mismatch here */
	error = convert_GCR_sector(gcr_track, gcr_track + NIB_TRACK_LENGTH, secbuf, 18, 0, id);

	/* no valid 18,0 sector */
	if((error != SECTOR_OK) && (error != ID_MISMATCH))
	{
		//printf("could not find directory sector!");
		return 0;
	}

	id[0] = secbuf[0xa3];
	id[1] = secbuf[0xa4];

	return 1;
}

BYTE
convert_GCR_sector(BYTE *gcr_start, BYTE *gcr_cycle, BYTE *d64_sector, int track, int sector, BYTE *id)
{
 	// we should later try to repair some common GCR errors
 	//	1) tri-bit error, in which 01110 is misinterpreted as 01000
	// 2) low frequency error, in which 10010 is misinterpreted as 11000

	BYTE header[10];        /* block header */
	BYTE hdr_chksum;        /* header checksum */
	BYTE blk_chksum;        /* block  checksum */
	BYTE *gcr_ptr, *gcr_end;
	BYTE *sectordata;
	BYTE error_code;
	size_t track_len;
	int i, j;

	if ((gcr_cycle == NULL) || (gcr_cycle <= gcr_start))
		return SYNC_NOT_FOUND;

	/* initialize sector data with Original Format Pattern */
	memset(d64_sector, 0x01, 260);
	d64_sector[0] = 0x07;   /* Block header mark */
	d64_sector[1] = 0x4b;   /* Use Original Format Pattern */

	for (blk_chksum = 0, i = 1; i < 257; i++)
		blk_chksum ^= d64_sector[i + 1];
	d64_sector[257] = blk_chksum;

	/* setup pointers */
	track_len = gcr_cycle - gcr_start;
	gcr_ptr = gcr_start;
	gcr_end = gcr_start + track_len;
	error_code = SECTOR_OK;

	/* Check for at least one Sync */
	if (!find_sync(&gcr_ptr, gcr_end))
		return SYNC_NOT_FOUND;

	/* Try to find a good block header for Track/Sector */
	error_code = HEADER_NOT_FOUND;

	//for (gcr_ptr = gcr_start; gcr_ptr < gcr_end-1; gcr_ptr++)
	for (gcr_ptr = gcr_start; gcr_ptr < gcr_end-10; gcr_ptr++)
	{
		if ((gcr_ptr[0] == 0xff) && (gcr_ptr[1] == 0x52))
		{
			gcr_ptr++;
			memset(header, 0, 10);

			convert_4bytes_from_GCR(gcr_ptr, header);
			convert_4bytes_from_GCR(gcr_ptr+5, header+4);

			if ((header[0] == 0x08) && (header[2] == sector) && (header[3] == track) )
			{
				/* this is the header we are searching for */
				error_code = SECTOR_OK;
				break;
			}
			if(verbose>2) printf("{1:%.2x, 2:%.2x, 3:%.2x, 4:%.2x, 5:%.2x}{I:%.2x, T:%.2d, S:%.2d}\n",
				gcr_ptr[1], gcr_ptr[2], gcr_ptr[3], gcr_ptr[4], gcr_ptr[5], header[0],header[3],header[2]);
		}
	}

	if(error_code != SECTOR_OK)
		return error_code;

	/* Header checksum calc */
	hdr_chksum = 0;
	for (i = 1; i <= 4; i++)
		hdr_chksum = hdr_chksum ^ header[i];

	if (hdr_chksum != header[5])
		error_code = (error_code == SECTOR_OK) ? BAD_HEADER_CHECKSUM : error_code;

	/* check for correct disk ID */
	if (header[5] != id[0] || header[4] != id[1])
		error_code = (error_code == SECTOR_OK) ? ID_MISMATCH : error_code;

	/* verify that our header contains no bad GCR, since it can be false positive checksum match */
	for(j = 0; j < 10; j++)
	{
		if (is_bad_gcr(gcr_ptr - 1, 10, j))
			error_code = (error_code == SECTOR_OK) ? BAD_GCR_CODE : error_code;
	}

	/* done with header checks */
	if((error_code != SECTOR_OK) && (error_code != ID_MISMATCH))
		return error_code;

	/* check for data sector, it will always be the data following header */
	if (!find_sync(&gcr_ptr, gcr_end))
	{
		gcr_ptr = gcr_start;
		if (!find_sync(&gcr_ptr, gcr_end))
			return DATA_NOT_FOUND;
	}

	for (i = 0, sectordata = d64_sector; i < 65; i++)
	{
		convert_4bytes_from_GCR(gcr_ptr, sectordata);

		if(verbose>3)
			printf("%.4x: %.2x%.2x%.2x%.2x%.2x --- %.2x%.2x%.2x%.2x\n", (i*4),
				gcr_ptr[0], gcr_ptr[1], gcr_ptr[2], gcr_ptr[3], gcr_ptr[4],
				sectordata[0], sectordata[1], sectordata[2], sectordata[3]);

		gcr_ptr += 5;
		sectordata += 4;
	}

	/* check for Block header mark */
	if (d64_sector[0] != 0x07)
	{
		error_code = (error_code == SECTOR_OK) ? DATA_NOT_FOUND : error_code;
		if(verbose>3) printf("\nIncorrect Block Header: 0x%.2x != 0x07\n", d64_sector[0]);
	}

	/* Block checksum calc */
	for (i = 1, blk_chksum = 0; i <= 256; i++)
		blk_chksum ^= d64_sector[i];

	if (blk_chksum != d64_sector[257])
		error_code = (error_code == SECTOR_OK) ? BAD_DATA_CHECKSUM : error_code;

	/* verify that our data contains no bad GCR, since it can be false positive checksum match */
	for(j = 0; j < 320; j++)
	{
		if (is_bad_gcr(gcr_ptr - 325, 320, j))
			error_code = (error_code == SECTOR_OK) ? BAD_GCR_CODE : error_code;
	}
	return error_code;
}

void
convert_sector_to_GCR(BYTE * buffer, BYTE * ptr, int track, int sector, BYTE * diskID, int error)
{
	int i;
	BYTE buf[4], databuf[0x104], chksum;
	BYTE tempID[3];

	memcpy(tempID, diskID, 3);
	memset(ptr, 0x55, SECTOR_SIZE + sector_gap_length[track]);	/* 'unformat' GCR sector */

	if (error == SYNC_NOT_FOUND)
		return;

	if (error == HEADER_NOT_FOUND)
	{
		ptr += 24;
	}
	else
	{
		memset(ptr, 0xff, SYNC_LENGTH);	/* Sync */
		ptr += SYNC_LENGTH;

		if (error == ID_MISMATCH)
		{
			tempID[0] ^= 0xff;
			tempID[1] ^= 0xff;
		}

		buf[0] = 0x08;	/* Header identifier */
		buf[1] = (BYTE) (sector ^ track ^ tempID[1] ^ tempID[0]);
		buf[2] = (BYTE) sector;
		buf[3] = (BYTE) track;

		if (error == BAD_HEADER_CHECKSUM)
			buf[1] ^= 0xff;

		convert_4bytes_to_GCR(buf, ptr);
		ptr += 5;
		buf[0] = tempID[1];
		buf[1] = tempID[0];
		buf[2] = buf[3] = 0x0f;
		convert_4bytes_to_GCR(buf, ptr);
		ptr += 5;
		memset(ptr, 0x55, HEADER_GAP_LENGTH);	/* Header Gap */
		ptr += HEADER_GAP_LENGTH;
	}

	if (error == DATA_NOT_FOUND)
		return;

	memset(ptr, 0xff, SYNC_LENGTH);	/* Sync */
	ptr += SYNC_LENGTH;

	chksum = 0;
	databuf[0] = 0x07;
	for (i = 0; i < 0x100; i++)
	{
		databuf[i + 1] = buffer[i];
		chksum ^= buffer[i];
	}

	if (error == BAD_DATA_CHECKSUM)
		chksum ^= 0xff;

	databuf[0x101] = chksum;
	databuf[0x102] = 0;	/* 2 bytes filler */
	databuf[0x103] = 0;

	for (i = 0; i < 65; i++)
	{
		convert_4bytes_to_GCR(databuf + (4 * i), ptr);
		ptr += 5;
	}

	memset(ptr, 0x55, sector_gap_length[track]);	 /* tail gap*/
	ptr += sector_gap_length[track];
	//memset(ptr, 0x55, SECTOR_GAP_LENGTH);	 /* tail gap*/
	//ptr += SECTOR_GAP_LENGTH;
}

size_t
find_track_cycle_headers(BYTE ** cycle_start, BYTE ** cycle_stop, size_t cap_min, size_t cap_max)
{
	BYTE *nib_track;	/* start of nibbled track data */
	BYTE *start_pos;	/* start of periodic area */
	BYTE *cycle_pos;	/* start of cycle repetition */
	BYTE *stop_pos;		/* maximum position allowed for cycle */
	BYTE *data_pos;		/* cycle search variable */
	BYTE *p1, *p2;		/* local pointers for comparisons */

	nib_track = *cycle_start;
	stop_pos = nib_track + NIB_TRACK_LENGTH - gap_match_length;
	cycle_pos = NULL;

	/* try to find a normal track cycle  */
	for (start_pos = nib_track;; find_header(&start_pos, stop_pos))
	{
		if ((data_pos = start_pos + cap_min) >= stop_pos)
			break;	/* no cycle found */

		while (find_header(&data_pos, stop_pos))
		{
			p1 = start_pos;
			cycle_pos = data_pos;

			for (p2 = cycle_pos; p2 < stop_pos;)
			{
				/* try to match all remaining syncs, too */
				if (memcmp(p1, p2, gap_match_length) != 0)
				{
					cycle_pos = NULL;
					break;
				}
				if (!find_header(&p1, stop_pos))
					break;
				if (!find_header(&p2, stop_pos))
					break;
			}

			if ((cycle_pos != NULL) && (check_valid_data(data_pos, gap_match_length)))
			{
				*cycle_start = start_pos;
				*cycle_stop = cycle_pos;
				return (cycle_pos - start_pos);
			}
		}
	}

	/* we got nothing useful, return it all */
	*cycle_start = nib_track;
	*cycle_stop = nib_track + NIB_TRACK_LENGTH;
	return NIB_TRACK_LENGTH;
}

size_t
find_track_cycle_syncs(BYTE ** cycle_start, BYTE ** cycle_stop, size_t cap_min, size_t cap_max)
{
	BYTE *nib_track;	/* start of nibbled track data */
	BYTE *start_pos;	/* start of periodic area */
	BYTE *cycle_pos;	/* start of cycle repetition */
	BYTE *stop_pos;		/* maximum position allowed for cycle */
	BYTE *data_pos;		/* cycle search variable */
	BYTE *p1, *p2;		/* local pointers for comparisons */

	nib_track = *cycle_start;
	stop_pos = nib_track + NIB_TRACK_LENGTH - gap_match_length;
	cycle_pos = NULL;

	/* try to find a normal track cycle  */
	for (start_pos = nib_track;; find_sync(&start_pos, stop_pos))
	{
		if ((data_pos = start_pos + cap_min) >= stop_pos)
			break;	/* no cycle found */

		while (find_sync(&data_pos, stop_pos))
		{
			p1 = start_pos;
			cycle_pos = data_pos;

			for (p2 = cycle_pos; p2 < stop_pos;)
			{
				/* try to match all remaining syncs, too */
				if (memcmp(p1, p2, gap_match_length) != 0)
				{
					cycle_pos = NULL;
					break;
				}
				if (!find_sync(&p1, stop_pos))
					break;
				if (!find_sync(&p2, stop_pos))
					break;
			}

			if ((cycle_pos != NULL) && (check_valid_data(data_pos, gap_match_length)))
			{
				*cycle_start = start_pos;
				*cycle_stop = cycle_pos;
				return (cycle_pos - start_pos);
			}
		}
	}

	/* we got nothing useful, return it all */
	*cycle_start = nib_track;
	*cycle_stop = nib_track + NIB_TRACK_LENGTH;
	return NIB_TRACK_LENGTH;
}

size_t
find_track_cycle_raw(BYTE ** cycle_start, BYTE ** cycle_stop, size_t cap_min, size_t cap_max)
{
	BYTE *nib_track;	/* start of nibbled track data */
	BYTE *start_pos;	/* start of periodic area */
	BYTE *cycle_pos;	/* start of cycle repetition */
	BYTE *stop_pos;		/* maximum position allowed for cycle */
	BYTE *p1, *p2;		/* local pointers for comparisons */

	nib_track = *cycle_start;
	start_pos = nib_track;
	stop_pos = nib_track + NIB_TRACK_LENGTH - gap_match_length;
	cycle_pos = NULL;

	/* try to find a track cycle ignoring sync  */
	for (p1 = start_pos; p1 < stop_pos; p1++)
	{
		/* now try to match it */
		for (p2 = p1 + (cap_min + CAP_ALLOWANCE); p2 < stop_pos; p2++)
		//for (p2 = p1 + cap_max; p2 > p1 + cap_min; p2--)
		{
			/* try to match data */
			if (memcmp(p1, p2, gap_match_length) != 0)
				cycle_pos = NULL;
			else
				cycle_pos = p2;

			/* we found one! */
			if ((cycle_pos != NULL) && (check_valid_data(cycle_pos, gap_match_length)))
			{
				*cycle_start = p1;
				*cycle_stop = cycle_pos;
				return (cycle_pos - p1);
			}
		}
	}

	/* we got nothing useful */
	*cycle_start = nib_track;
	*cycle_stop = nib_track + NIB_TRACK_LENGTH;
	return NIB_TRACK_LENGTH;
}

int
check_valid_data(BYTE * data, int matchlen)
{
	/* makes a simple assumption whether this is good data to match track cycle overlap */
	int i, redund=0;

	for (i = 0; i < matchlen; i++)
	{
		if(data[i] == 0xff) return 0; /* sync marks */
		if((data[i] == data[i+1]) && (data[i+1] == data[i+2])) redund++;  /* repeating bytes */
		if((data[i] == data[i+2]) && (data[i+1] == data[i+3])) redund++; /* alternating bytes (as seen on PirateSlayer tracks)  */

		if(redund>2)
		{
			//printf("{RB}");
			return 0;
		}

		/* check we aren't matching gap data (GCR is 555555 or AAAAAA) */
		if((data[i] == 0x55) && (data[i+1] == 0xaa) && (data[i+2] == 0x55)) return 0;
		if((data[i] == 0xaa) && (data[i+1] == 0x55) && (data[i+2] == 0xaa)) return 0;
		if((data[i] == 0x5a) && (data[i+1] == 0xa5) && (data[i+2] == 0x5a)) return 0;

		/* check we aren't matching gap data (GCR encoded 555555 or AAAAAA) */
		//if((data[i] == 0x52) && (data[i+1] == 0xd4) && (data[i+2] == 0xb5)) return 0;
		//if((data[i] == 0x2d) && (data[i+1] == 0x4b) && (data[i+2] == 0x52)) return 0;
	}
	return 1;
}

BYTE *
find_sector0(BYTE * work_buffer, size_t tracklen, size_t * p_sectorlen)
{
	BYTE *pos, *buffer_end, *sync_last;

	pos = work_buffer;
	buffer_end = work_buffer + 2 * tracklen - 10;
	*p_sectorlen = 0;

	if (!find_sync(&pos, buffer_end))
		return NULL;

	sync_last = pos;

	/* try to find sector 0 */
	while (pos < buffer_end)
	{
		if (!find_sync(&pos, buffer_end))
			return NULL;
		if (pos[0] == 0x52 && (pos[1] & 0xc0) == 0x40 &&
		  (pos[2] & 0x0f) == 0x05 && (pos[3] & 0xfc) == 0x28)
		{
			/* this is inaccurate if sector 0 is the first thing found */
			/* *p_sectorlen = pos - sync_last; GCR_BLOCK_LEN;  */
			*p_sectorlen = GCR_BLOCK_LEN;
			break;
		}
		sync_last = pos;
	}

	/* find last GCR byte before sync */
	do
	{
		pos -= 1;
		if (pos == work_buffer)
			pos += tracklen;
	} while (*pos == 0xff);

	/* move to first sync byte */
	pos += 1;
	while (pos >= work_buffer + tracklen)
		pos -= tracklen;

	//if(*(pos-1)&1)
	//	return pos - 1;  // go to  last byte that contains first few bits of sync
	//else
		return pos; // return at first full byte of sync
}

BYTE *
find_sector_gap(BYTE * work_buffer, size_t tracklen, size_t * p_sectorlen)
{
	size_t gap, maxgap;
	BYTE *pos;
	BYTE *buffer_end;
	BYTE *sync_last;
	BYTE *sync_max;

	pos = work_buffer;
	buffer_end = work_buffer + 2 * tracklen - 10;
	*p_sectorlen = 0;

	if (!find_sync(&pos, buffer_end))
		return NULL;

	sync_last = pos;
	sync_max = pos;
	maxgap = 0;

	/* try to find biggest (sector) gap */
	while (pos < buffer_end)
	{
		if (!find_header(&pos, buffer_end))
			break;

		gap = pos - sync_last;
		if (gap > maxgap)
		{
			maxgap = gap;
			sync_max = pos;
		}
		sync_last = pos;
	}
	*p_sectorlen = maxgap;

	if (maxgap == 0)
		return NULL;	/* no gap found */

	/* find last GCR byte before header */
	pos = sync_max;
	do
	{
		pos -= 1;
		if (pos == work_buffer)
			pos += tracklen;

	} while (*pos == 0xff);

	/* move to first sync GCR byte */
	pos += 1;
	while (pos >= work_buffer + tracklen)
		pos -= tracklen;

	//if(*(pos-1)&1)
	//	return pos - 1;  // go to  last byte that contains first few bits of sync
	//else
		return pos; // return at first full byte of sync
}

/* checks if there is any reasonable section of formatted (GCR) data */
int check_formatted(BYTE *gcrdata, size_t length)
{
	size_t i, run = 0;

	/* try to find longest good gcr run */
	for (i = 0; i < length; i++)
	{
		if (is_bad_gcr(gcrdata, length, i))
			run = 0;
		else
			run++;

		if (run >= GCR_MIN_FORMATTED)
			return 1;
	}
	return 0;
}

/*
   Try to extract one complete cycle of GCR data from an 8kB buffer.
   Align track to sector gap if possible, else align to sector 0,
   else copy cyclic loop from begin of source.
   If buffer has no good GCR, return tracklen = 0;
   [Input]  destination buffer, source buffer
   [Return] length of copied track fragment
*/
size_t
extract_GCR_track(BYTE *destination, BYTE *source, BYTE *align, int track, size_t cap_min, size_t cap_max)
{
	BYTE work_buffer[NIB_TRACK_LENGTH*2];	/* working buffer */
	BYTE *cycle_start;	/* start position of cycle */
	BYTE *cycle_stop;	/* stop position of cycle  */
	BYTE *sector0_pos;	/* position of sector 0 */
	BYTE *sectorgap_pos;/* position of sector gap */
	BYTE *longsync_pos;	/* position of longest sync run */
	BYTE *badgap_pos;	/* position of bad gcr bit run */
	BYTE *marker_pos;	/* generic marker used by protection handlers */
	size_t track_len;
	size_t sector0_len;	/* length of gap before sector 0 */
	size_t sectorgap_len;	/* length of longest gap */
	BYTE fake_density = 0;
	int i ,j;

	sector0_pos = NULL;
	sectorgap_pos = NULL;
	longsync_pos = NULL;
	badgap_pos = NULL;
	marker_pos = NULL;

	/* ignore minumum capacity by RPM/density */
	if(!cap_min_ignore)
	{
		cap_min -= CAP_ALLOWANCE;
		cap_max += CAP_ALLOWANCE;
	}

	/* if this track doesn't have enough formatted data, return blank */
	if (!check_formatted(source, NIB_TRACK_LENGTH))
		return 0;

	/* if this track is all sync, return */
	if(check_sync_flags(source, fake_density, NIB_TRACK_LENGTH) & BM_FF_TRACK)
	{
		if(verbose) printf("KILLER! ");
		memcpy(destination, source, NIB_TRACK_LENGTH);
		return NIB_TRACK_LENGTH;
	}

	cycle_start = source;
	memset(work_buffer, 0, sizeof(work_buffer));
	memcpy(work_buffer, cycle_start, NIB_TRACK_LENGTH);

	/* find cycle */
	if(verbose>1) printf("[H");
	find_track_cycle_headers(&cycle_start, &cycle_stop, cap_min, cap_max);
	track_len = cycle_stop - cycle_start;

	/* second pass to find a cycle in track w/non-standard headers */
	if ((track_len > cap_max) || (track_len < cap_min))
	{
		if(verbose>1) printf("/S] ");
		find_track_cycle_syncs(&cycle_start, &cycle_stop, cap_min, cap_max);
		track_len = cycle_stop - cycle_start;
	}

	/* third pass to find a cycle in track w/non-standard headers */
	if ((track_len > cap_max) || (track_len < cap_min))
	{
		if(verbose>1) printf("/R] ");
		find_track_cycle_raw(&cycle_start, &cycle_stop, cap_min, cap_max);
		track_len = cycle_stop - cycle_start;
	}

	if (track_len <= cap_min)
	{
		if(verbose>1) printf("/+");
		track_len += (cap_max-cap_min)/2;
	}

	if(verbose>2)
	{
		if (track_len > cap_max)
			printf("[LONG, max=%d<%d] ",cap_max, track_len);
		if(track_len < cap_min)
			printf("[SHORT, min=%d>%d] ", cap_min, track_len);

		printf("{cycle:");
		for(i=0;i<gap_match_length;i++)
			printf("%.2x",cycle_start[i]);
		printf("}");
	}

	/* copy twice the data to work buffer */
	memcpy(work_buffer, cycle_start, track_len);
	memcpy(work_buffer + track_len, cycle_start, track_len);

	/* print sector0 offset from beginning of data (for index hole check) */
	if(verbose>1)
	{
		sector0_pos = find_sector0(work_buffer, track_len, &sector0_len);
		printf("{sec0=%.4d;len=%d} ",(int)(sector0_pos - work_buffer), sector0_len);
	}

	/* forced track alignments */
	if (align_map[track] != ALIGN_NONE)
	{
		if (align_map[track] == ALIGN_VMAX_CW)
		{
			*align = ALIGN_VMAX_CW;
			marker_pos = align_vmax_cw(work_buffer, track_len);

			if(!marker_pos)
				align_map[track] = ALIGN_VMAX;
		}

		if (align_map[track] == ALIGN_VMAX)
		{
			*align = ALIGN_VMAX;
			marker_pos = align_vmax_new(work_buffer, track_len);
		}

		if (align_map[track] == ALIGN_PSLAYER)
		{
			*align = ALIGN_PSLAYER;
			marker_pos = align_pirateslayer(work_buffer, track_len);
		}

		if (align_map[track] == ALIGN_RAPIDLOK)
		{
			*align = ALIGN_RAPIDLOK;
			marker_pos = align_rl_special(work_buffer, track_len);
		}

		if (align_map[track] == ALIGN_AUTOGAP)
		{
			*align = ALIGN_AUTOGAP;
			marker_pos = auto_gap(work_buffer, track_len);
		}

		if (align_map[track] == ALIGN_LONGSYNC)
		{
			*align = ALIGN_LONGSYNC;
			marker_pos = find_long_sync(work_buffer, track_len);
		}

		if (align_map[track] == ALIGN_BADGCR)
		{
			*align = ALIGN_BADGCR;
			marker_pos = find_bad_gap(work_buffer, track_len);
		}

		if (align_map[track] == ALIGN_GAP)
		{
			*align = ALIGN_GAP;
			marker_pos = find_sector_gap(work_buffer, track_len, &sectorgap_len);
		}

		if (align_map[track] == ALIGN_SEC0)
		{
			*align = ALIGN_SEC0;
			marker_pos = find_sector0(work_buffer, track_len, &sector0_len);
		}

		if (align_map[track] == ALIGN_RAW)
		{
			*align = ALIGN_RAW;
			marker_pos = work_buffer;
		}

		/* we found a protection track */
		if (marker_pos)
		{
			memcpy(destination, marker_pos, track_len);
			goto aligned;
		}
	}

	/* tracks with no detected cycle */
	//if (track_len == NIB_TRACK_LENGTH)
	//{
		/* if there is sync, align to the longest one */
		//marker_pos = find_long_sync(work_buffer, track_len);
		//if (marker_pos)
		//{
		//	memcpy(destination, marker_pos, track_len);
		//	*align = ALIGN_LONGSYNC;
		//	goto aligned;
		//}

		/* we aren't dealing with a normal track here, so autogap it */
		//marker_pos = auto_gap(work_buffer, track_len);
		//if (marker_pos)
		//{
		//	memcpy(destination, marker_pos, track_len);
		//	*align = ALIGN_AUTOGAP;
		//	goto aligned;
		//}
	//}

	/* try to guess original alignment on "normal" sized tracks */
	sector0_pos = find_sector0(work_buffer, track_len, &sector0_len);
	sectorgap_pos = find_sector_gap(work_buffer, track_len, &sectorgap_len);

	if(verbose>2)
		printf("{gap=%.4d;len=%d) ", (int)(sectorgap_pos-work_buffer), (int)sectorgap_len);

	if((sectorgap_pos-work_buffer == sector0_pos-work_buffer) &&
		(sectorgap_pos != NULL) &&	(sector0_pos != NULL) && (verbose>1))
		printf("(sec0=gap) ");

	/* if (sectorgap_len >= sector0_len + 0x40) */ /* Burstnibbler's calc */
	if (sectorgap_len > GCR_BLOCK_DATA_LEN + SIGNIFICANT_GAPLEN_DIFF)
	{
		*align = ALIGN_GAP;
		memcpy(destination, sectorgap_pos, track_len);
		goto aligned;
	}

	/* no large gap found, try sector 0 */
	if (sector0_len != 0)
	{
		*align = ALIGN_SEC0;
		memcpy(destination, sector0_pos, track_len);
		goto aligned;
	}

	/* no sector 0 found, use gap anyway */
	if (sectorgap_len)
	{
		memcpy(destination, sectorgap_pos, track_len);
		*align = ALIGN_GAP;
		goto aligned;
	}

	/* if there is sync, align to the longest one */
	//marker_pos = find_long_sync(work_buffer, track_len);
	//if (marker_pos)
	//{
	//	memcpy(destination, marker_pos, track_len);
	//	*align = ALIGN_LONGSYNC;
	//	goto aligned;
	//}

	/* we aren't dealing with a normal track here, so autogap it */
	marker_pos = auto_gap(work_buffer, track_len);
	if (marker_pos)
	{
		memcpy(destination, marker_pos, track_len);
		*align = ALIGN_AUTOGAP;
		goto aligned;
	}

	/* we give up, just return everything */
	memcpy(destination, work_buffer, track_len);
	*align = ALIGN_NONE;
	goto aligned;

aligned:
	i=j=0;
	if(verbose>1)
	{
		if(verbose>1) printf("{align:");
		while((i<gap_match_length) && (i<(int)track_len))
		{
			if(destination[j] != 0xff)
			{
				if(verbose>1) printf("%.2x",destination[j]);
				j++; i++;
			}
			else j++;
		}
		printf("} ");
	}
	return track_len;
}

size_t
lengthen_sync(BYTE *buffer, size_t length, size_t length_max)
{
        size_t added;
        BYTE *source, *newp, *end;
        BYTE newbuf[NIB_TRACK_LENGTH];

        added = 0;
        end = buffer + length - 1;
        source = buffer;
        newp = newbuf;

        if (length >= length_max)
                return 0;

        /* wrap alignment */
        //if( ((*(end-1) & 0x01) == 0x01) && (*source == 0xff) && (*(source+1) != 0xff) )
        //{
        //        *(newp++) = 0xff;
        //        added++;
        //}
        //*(newp++) = *(source++);

        do
        {
				//if(((*(source-1)&0x01)==0x01)&&(*source==0xff)&&(*(source+1)!= 0xff)&&(length+added<=length_max))
                if((*source==0xff)&&(*(source+1)!= 0xff))
                {
                        *(newp++) = 0xff;
                        added++;
                }
                *(newp++) = *(source++);

        } while (source <= end);

        memcpy(buffer, newbuf, length+added);
        return added;
}


size_t
kill_partial_sync(BYTE * gcrdata, size_t length, size_t length_max)
{
	size_t sync_cnt = 0;
	size_t sync_len[1000];
	size_t sync_pos[1000];
	BYTE sync_pre[1000];
	BYTE sync_pre2[1000];
	size_t i, locked, total=0;

	memset(sync_len, 0, sizeof(sync_len));
	memset(sync_pos, 0, sizeof(sync_pos));
	memset(sync_pre, 0, sizeof(sync_pre));
	memset(sync_pre2, 0, sizeof(sync_pre2));

	// count syncs/lengths
	for (locked=0, i=0; i<length-1; i++)
	{
		if (locked)
		{
			if (gcrdata[i] == 0xff)
				sync_len[sync_cnt]++;
			else
				locked = 0; // end of sync
		}
		//else if (gcrdata[i] == 0xff) // not full sync, only last 8 bits
		else if(((gcrdata[i] & 0x01) == 0x01) && (gcrdata[i+1] == 0xff)) // 10 bits
		{
			locked = 1;
			sync_cnt++;
			sync_len[sync_cnt] = 1;
			sync_pos[sync_cnt] = i;
			sync_pre[sync_cnt] = gcrdata[i];
			sync_pre2[sync_cnt] = gcrdata[i-1];
		}

	}

	if(verbose>1) printf("\nSYNCS:%d\n", sync_cnt);
	for (i=1; i<=sync_cnt; i++)
	{
		if(verbose>1) printf("(%d,%d,%x%x)\n", sync_pos[i], sync_len[i], sync_pre2[i], sync_pre[i]);

		gcrdata[sync_pos[i]] = sync_pre2[i];
	}

	return 0;
}

/*
	This strips exactly one byte at minrun from each
	eligible run when called.  It can be called repeatedly
	for a proportional reduction.
 */
size_t
strip_runs(BYTE * buffer, size_t length, size_t length_max, size_t minrun, BYTE target)
{
	/* minrun is number of bytes to leave behind */
	size_t run, skipped;
	BYTE *source, *end;

	run = 0;
	skipped = 0;
	end = buffer + length;

	for (source = buffer; source < end; source++)
	{
		if ( (*source == target) && (length - skipped >= length_max) )
		{
			if (run == minrun)
				skipped++;
			else
				*buffer++ = target;

			run++;
		}
		else
		{
			run = 0;
			*buffer++ = *source;
		}
	}
	return skipped;
}

/* try to shorten inert data until length <= length_max */
size_t
reduce_runs(BYTE * buffer, size_t length, size_t length_max, size_t minrun, BYTE target)
{
	/* minrun is number of bytes to leave behind */
	size_t skipped;

	do
	{
		if (length <= length_max)
			return (length);

		skipped = strip_runs(buffer, length, length_max, minrun, target);
		length -= skipped;
	}
	while (skipped > 0 && length > length_max);

	return (length);
}

size_t
strip_gaps(BYTE * buffer, size_t length)
{
	int skipped;
	BYTE *source, *end;

	skipped = 0;
	end = buffer + length;

	/* this is crude, I know */
	/* this will find a sync that is of sufficient length and strip the byte before it */
	/* this can damage real data if done too much and will damage signatures before a sync */
	for (source = buffer; source < end - 2; source++)
	{
		if ( (*source != 0xff) && (*(source+1) == 0xff) && (*(source+2) == 0xff) )
			skipped++;
		else
			*buffer++ = *source;
	}
	return skipped;
}

/* try to shorten tail gaps until length <= length_max */
size_t
reduce_gaps(BYTE * buffer, size_t length, size_t length_max)
{
	size_t skipped;

	do
	{
		if (length <= length_max)
			return (length);

		skipped = strip_gaps(buffer, length);
		length -= skipped;
	}
	while (skipped > 0 && length > length_max);

	return (length);
}

/*
	this routine checks the track data and makes simple decisions
	about the special cases of being all sync or having no sync
*/
BYTE
check_sync_flags(BYTE *gcrdata, int density, size_t length)
{
	size_t i;
	size_t syncs=0;

	/* if empty, we have no sync */
	if(!length)
		return (BYTE)(density |= BM_NO_SYNC);

	/* check manually for SYNCKILL */
	for (i=0; i<length-1; i++)
	{
		/* check for sync mark */
		/*if ( ((gcrdata[i] & 0x03) == 0x03) && (gcrdata[i+1] == 0xff) )  syncs++;*/

		/* NOTE: This is not flagging true "hardware detected" sync marks, only the last 7 bits of it */
		if ((gcrdata[i] & 0x7f) == 0x7f) syncs++;
	}

	if(!syncs)
		density |= BM_NO_SYNC;
	else if (syncs >= length - 3)  /* sometimes we get a small glitch on killer tracks from the mastering device */
		density |= BM_FF_TRACK;

	/* else do nothing */
	return ((BYTE)(density & 0xff));
}

size_t
compare_tracks(BYTE *track1, BYTE *track2, size_t length1, size_t length2, int same_disk, char *outputstring)
{
	size_t match, byte_match, j, k;
	size_t sync_diff, shift_diff, presync_diff, gap_diff, badgcr_diff, size_diff, byte_diff;
	size_t offset;
	char tmpstr[256];

	byte_match = 0;
	byte_diff = 0;
	sync_diff = 0;
	shift_diff = 0;
	presync_diff = 0;
	gap_diff = 0;
	badgcr_diff = 0;
	size_diff= 0;
	offset = 0;
	outputstring[0] = '\0';

	if (length1 > 0 && length2 > 0)
	{
		for (j = k = 0; (j < length2) && (k < length1); j++, k++)
		{
			/* we ignore sync length differences */
			if ((track1[j] == 0xff) && (track2[k] == 0xff) )
				continue;

			if (track1[j] == 0xff)
			{
				sync_diff++;
				k--;
				continue;
			}

			if (track2[k] == 0xff)
			{
				//sync_diff++;
				j--;
				continue;
			}

			/* we ignore pre-sync differences */
			if ( (track1[j] != 0xff) && (track1[j+1] == 0xff) )
			{
				presync_diff++;
				k--;
				continue;
			}

			if ( (track2[k] != 0xff)  && (track2[k+1] == 0xff) )
			{
				//presync_diff++;
				j--;
				continue;
			}

			/* we ignore inert bitshift differences */
			if ( ((track1[j] == 0x55) && (track2[k] == 0xaa)) || ((track1[j] == 0xaa) && (track2[k] == 0x55)) )
			{
				shift_diff++;
				continue;
			}

			/* we ignore bad gcr bytes */
			if (is_bad_gcr(track1, length1, j))
			{
				badgcr_diff++;
				k--;
				continue;
			}

			if (is_bad_gcr(track2, length2, k))
			{
				//badgcr_diff++;
				j--;
				continue;
			}

			if (track1[j] == track2[k])
			{
				byte_match++;
				continue;
			}

			/* it just didn't work out. :) */
			if(verbose>2)
				printf("(%.4d:%.2x!=%.2x)",(int)j,track1[j],track2[k]);

			byte_diff++;
		}

		if ( (j < length1 - 1) || (k < length2 - 1))
			size_diff++;

		/* we got to the end of one of them OK and not all sync/bad gcr */
		if ((j >= length1 - 1 || k >= length2 - 1) && (sync_diff < 0x100 && badgcr_diff < 0x100))
			match = 1;
	}

	if(byte_match)
	{
			sprintf(tmpstr, "(match:%d)", byte_match);
			strcat(outputstring, tmpstr);
	}

	if(byte_diff)
	{
			sprintf(tmpstr, "(diff:%d)", byte_diff);
			strcat(outputstring, tmpstr);
	}
	else
		match = 1;

	if (sync_diff)
	{
		sprintf(tmpstr, "(sync:%d)", sync_diff);
		strcat(outputstring, tmpstr);
	}

	if (shift_diff)
	{
		sprintf(tmpstr, "(shift:%d)", shift_diff);
		strcat(outputstring, tmpstr);
	}

	if (presync_diff)
	{
		sprintf(tmpstr, "(presync:%d}", presync_diff);
		strcat(outputstring, tmpstr);
	}

	if (gap_diff)
	{
		sprintf(tmpstr, "(gap:%d)", gap_diff);
		strcat(outputstring, tmpstr);
	}

	if (badgcr_diff)
	{
		sprintf(tmpstr, "(weak:%d)", badgcr_diff);
		strcat(outputstring, tmpstr);
	}

	if (size_diff)
	{
		sprintf(tmpstr, "(size:%d)", size_diff);
		strcat(outputstring, tmpstr);
	}

	return byte_match + sync_diff + presync_diff + shift_diff + gap_diff + badgcr_diff;
	//return byte_diff;
}

size_t
compare_sectors(BYTE * track1, BYTE * track2, size_t length1, size_t length2, BYTE * id1, BYTE * id2, int track, char * outputstring)
{
	int sec_match, numsecs;
	int sector, error1, error2, empty;
	int i, j, k, diff;
	BYTE checksum1, checksum2;
	BYTE secbuf1[260], secbuf2[260];
	char tmpstr[256];
	unsigned int crcresult1, crcresult2;

	sec_match = 0;
	numsecs = 0;
	checksum1 = 0;
	checksum2 = 0;
	outputstring[0] = '\0';

	crcInit();

	if ( (length1 == 0) || (length2 == 0) ||
		 (length1 == NIB_TRACK_LENGTH) || (length2 == NIB_TRACK_LENGTH))
		return 0;

	/* check for sector matches */
	for (sector = 0; sector < sector_map[track/2]; sector++)
	{
		numsecs++;

		memset(secbuf1, 0, sizeof(secbuf1));
		memset(secbuf2, 0, sizeof(secbuf2));
		tmpstr[0] = '\0';

		error1 = convert_GCR_sector(track1, track1+length1, secbuf1, track/2, sector, id1);
		error2 = convert_GCR_sector(track2, track2+length2, secbuf2, track/2, sector, id2);

		/* compare data returned */
		checksum1 = 0;
		checksum2 = 0;
		empty = 0;

		for (i = 1; i <= 256; i++)
		{
			checksum1 ^= secbuf1[i];
			checksum2 ^= secbuf2[i];

			if (secbuf1[i] == 0x01)
				empty++;
		}

		crcresult1 = crcFast(&secbuf1[1], 256);
		crcresult2 = crcFast(&secbuf2[1], 256);

		/* continue checking */
		if ((checksum1 == checksum2) && (error1 == error2) && (crcresult1 == crcresult2))
		{
			if(error1 == SECTOR_OK)
			{
				/*sprintf(tmpstr,"S%d: sector data match\n",sector);*/
				sec_match++;
			}
			else
			{
				if(verbose) sprintf(tmpstr,"T%.1fS%d: Non-CBM (%.2x/E%d)(%.2x/E%d)\n",
					(float)track/2,sector,checksum1,error1,checksum2,error2);
			}
		}
		else
		{
			sprintf(tmpstr, "T%.1fS%d Mismatch (%.2x/E%d/CRC:%x) (%.2x/E%d/CRC:%x)\n",
				(float)track/2, sector, checksum1, error1, crcresult1, checksum2, error2, crcresult2);

			if(verbose)
			{
				printf(tmpstr, "T%.1fS%d Mismatch (%.2x/E%d/CRC:%x) (%.2x/E%d/CRC:%x)\n",
					(float)track/2, sector, checksum1, error1, crcresult1, checksum2, error2, crcresult2);

				printf("T%.1fS%d converted from GCR:\n", (float)track/2, sector);

				/* this prints out sector contents, which is not always terminal compatible */
				for (i=0; i<256; i+=16)
				{

					if((memcmp(secbuf1+i,secbuf2+i,16)) || verbose>1)
					{
						printf("($%.2x) 1:", i);

						for(j=0; j<16; j++)
							printf("%.2x ", secbuf1[i+j]);

						for(j=0; j<16; j++)
						{
							if(secbuf1[i+j] >= 32)
								printf("%c", secbuf1[i+j]);
							else
								printf("%c", secbuf1[i+j]+32);
						}

						printf("\n($%.2x) 2:", i);

						for(k=0; k<16; k++)
						{
							printf("%.2x ", secbuf2[i+k]);
							if(secbuf2[i+k]!=secbuf1[i+k]) diff=1;
						}
						for(k=0; k<16; k++)
						{
							if(secbuf2[i+k] >= 32)
								printf("%c", secbuf2[i+k]);
							else
								printf("%c", secbuf2[i+k]+32);
						}

						if(diff) { printf(" DIFF"); diff=0; }
						printf("\n");
					}
				}

				if(verbose>1)
				{
					for(i=0;i<256;i++)
					{
						if(secbuf1[i] != secbuf2[i])
							printf("offset $%.2x: $%.2x!=$%.2x\n", i, secbuf1[i], secbuf2[i]);
					}
				}

				//getchar();
			}
		}
		strcat(outputstring, tmpstr);
	}
	return sec_match;
}

char frompetscii(char s)
{
        if((s>=65)&&(s<=90))
        	s-=32;
        else if((s>=97)&&(s<=122))
        	s+=32;

        return(s);
}

char topetscii(char s)
{
        if((s>=65)&&(s<=90))
        	s+=32;
        else if((s>=97)&&(s<=122))
        	s-=32;

        return(s);
}


/* check for CBM DOS errors */
size_t
check_errors(BYTE * gcrdata, size_t length, int track, BYTE * id, char * errorstring)
{
	int errors, sector;
	char tmpstr[16];
	BYTE secbuf[260], errorcode;

	errors = 0;
	errorstring[0] = '\0';

	for (sector = 0; sector < sector_map[track/2]; sector++)
	{
		errorcode = convert_GCR_sector(gcrdata, gcrdata + length, secbuf, (track/2), sector, id);

		if (errorcode != SECTOR_OK)
		{
			errors++;
			sprintf(tmpstr, "[E%dS%d]", errorcode, sector);
			strcat(errorstring, tmpstr);
		}
	}
	return errors;
}

/* check for CBM DOS empty sectors */
size_t
check_empty(BYTE * gcrdata, size_t length, int track, BYTE * id, char * errorstring)
{
	int i, empty, sector, errorcode;
	char tmpstr[16], temp_errorstring[256];
	BYTE secbuf[260];

	empty = 0;
	errorstring[0] = '\0';
	temp_errorstring[0] = '\0';

	for (sector = 0; sector < sector_map[track / 2]; sector++)
	{
		errorcode = convert_GCR_sector(gcrdata, gcrdata + length, secbuf, (track / 2), sector, id);

		if (errorcode == SECTOR_OK)
		{
			/* checks for empty (unused) sector */
			for (i = 2; i <= 256; i++)
			{
				if (secbuf[i] != 0x01)
				{
					/*printf("%d:%0.2x ",i,secbuf[i]);*/
					break;
				}
			}

			if (i == 257)
			{
				sprintf(tmpstr, "%d-", sector);
				strcat(temp_errorstring, tmpstr);
				empty++;
			}
		}
	}

	if (empty)
		sprintf(errorstring, "EMPTY:%d (%s)", empty, temp_errorstring);

	return (empty);
}

/*
 * Replace 'srcbyte' by 'dstbyte'
 * Returns total number of bytes replaced
 */
int
replace_bytes(BYTE * buffer, size_t length, BYTE srcbyte, BYTE dstbyte)
{
	size_t i;
	int replaced;

	replaced = 0;

	for (i = 0; i < length; i++)
	{
		if (buffer[i] == srcbyte)
		{
			buffer[i] = dstbyte;
			replaced++;
		}
	}
	return replaced;
}

/* Check if byte at pos contains a 000 bit combination */
size_t
is_bad_gcr(BYTE * gcrdata, size_t length, size_t pos)
{
	size_t lastbyte, mask, data;

	lastbyte = (pos == 0) ? gcrdata[length - 1] : gcrdata[pos - 1];
	data = ((lastbyte & 0x03) << 8) | gcrdata[pos];

	for (mask = (7 << 7); mask >= 7; mask >>= 1)
	{
		if ((data & mask) == 0)
			break;
	}
	return (mask >= 7);
}

/*
 * Check and "correct" bad GCR bits:
 * substitute bad GCR bytes by 0x00 until next good GCR byte
 * when two in a row or row+1 occur (bad bad -or- bad good bad)
 *
 * all known disks that use this for protection break out
 * of it with $55, $AA, or $FF byte.
 *
 * fix_first, fix_last not used normally because while "correct", the real hardware
 * is not this precise and it fails the protection checks sometimes.
 */

extern int fix_gcr;

size_t
check_bad_gcr(BYTE * gcrdata, size_t length)
{
	/* state machine definitions */
	enum ebadgcr { S_BADGCR_OK, S_BADGCR_ONCE_BAD, S_BADGCR_LOST };
	enum ebadgcr sbadgcr;
	size_t i, j, lastpos;
	size_t total, b_badgcr;
	size_t n_badgcr;
	size_t firstbad, lastbad;
	BYTE origdata[0x2000];

	/* if empty we are all "bad" GCR */
	if(!length)
		return NIB_TRACK_LENGTH;

	total = 0;
	lastpos = 0;
	firstbad = lastbad = 0;
	sbadgcr = S_BADGCR_OK;
	memcpy(origdata,gcrdata,length);

	for (i = 0; i < length - 1; i++)
	{
		b_badgcr = is_bad_gcr(gcrdata, length, i);
		n_badgcr = is_bad_gcr(gcrdata, length, i + 1);

		switch (sbadgcr)
		{
			case S_BADGCR_OK:
				if (b_badgcr)
				{
					total++;

					if(fix_gcr > 2)
					{
						sbadgcr = S_BADGCR_LOST;  /* most aggressive */
						gcrdata[lastpos] = 0x00;
					}
					else
					{
						sbadgcr = S_BADGCR_ONCE_BAD;
					}

					if(!firstbad) firstbad=lastpos;
				}
				else
				{
					if((firstbad) && (verbose>1))
					{
						if(memcmp(origdata+firstbad,gcrdata+firstbad,i-firstbad))
						{
							printf("badgcr(%x-%x):[",firstbad,i);
							for(j=firstbad;j<i;j++)
								printf("%x:",origdata[j]);
							printf("]=[");
							for(j=firstbad;j<i;j++)
								printf("%x:",gcrdata[j]);
							printf("]\n");
						}
						firstbad = 0;
					}
				}
				break;

			case S_BADGCR_ONCE_BAD:
				if ((b_badgcr) || ((fix_gcr>3) && (n_badgcr)) )
				{
					total++;
					sbadgcr = S_BADGCR_LOST;

					if(fix_gcr > 1)
					{
						fix_first_gcr(gcrdata, length, lastpos);
					}
					else if (fix_gcr > 2)
					{
						gcrdata[lastpos] = 0x00;
					}
				}
				else
					sbadgcr = S_BADGCR_OK;
				break;

			case S_BADGCR_LOST:
				if ((b_badgcr) || ((fix_gcr>3) && (n_badgcr)) )
				{
					total++;

					if (fix_gcr)
					{
						gcrdata[lastpos] = 0x00;
					}
				}
				else
				{
					sbadgcr = S_BADGCR_OK;

					if(fix_gcr > 1)
					{
						fix_last_gcr(gcrdata, length, lastpos);
					}
					else if(fix_gcr > 2)
					{
						gcrdata[lastpos] = 0x00;
					}
				}
				break;
		}
		lastpos = i;
	}
	return total;
}
