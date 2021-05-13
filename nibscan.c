/*
    NIBSCAN - part of the NIBTOOLS package for 1541/1571 disk image nibbling
	by Peter Rittwage <peter(at)rittwage(dot)com>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"
#include "prot.h"
#include "md5.h"
#include "lz.h"

int _dowildcard = 1;

char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };

int load_image(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length);
int compare_disks(void);
int scandisk(void);
int raw_track_info(BYTE *gcrdata, size_t length);
int dump_headers(BYTE * gcrdata, size_t length);
size_t check_fat(int track);
size_t check_rapidlok(int track);

BYTE compressed_buffer[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
BYTE file_buffer[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
BYTE track_buffer[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
BYTE track_buffer2[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
size_t track_length[MAX_HALFTRACKS_1541 + 2];
size_t track_length2[MAX_HALFTRACKS_1541 + 2];
BYTE track_density[MAX_HALFTRACKS_1541 + 2];
BYTE track_density2[MAX_HALFTRACKS_1541 + 2];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 2];
BYTE track_alignment2[MAX_HALFTRACKS_1541 + 2];

size_t fat_tracks[MAX_HALFTRACKS_1541 + 2];
size_t rapidlok_tracks[MAX_HALFTRACKS_1541 + 2];
size_t badgcr_tracks[MAX_HALFTRACKS_1541 + 2];

int start_track, end_track, track_inc;
int imagetype, mode;
int align, force_align;
int file_buffer_size;
int fix_gcr;
int reduce_sync;
int reduce_badgcr;
int reduce_gap;
int waitkey = 0;
int gap_match_length;
int cap_relax;
int verbose;
int rpm_real;
int auto_capacity_adjust;
int skew;
int align_disk;
int ihs;
int unformat_passes;
int capacity_margin;
int align_delay;
int cap_min_ignore;
int increase_sync = 0;
int presync = 0;
BYTE fillbyte = 0xfe;
BYTE drive = 8;
char * cbm_adapter = "";
int use_floppycode_srq = 0;
int override_srq = 0;
int extra_capacity_margin=5;
int sync_align_buffer=0;
int fattrack=0;
int track_match=0;
int old_g64=0;
int read_killer=1;
int backwards=0;

unsigned char md5_hash_result[16];
unsigned char md5_dir_hash_result[16];
unsigned char md5_hash_result2[16];
unsigned char md5_dir_hash_result2[16];
int crc, crc_dir, crc2, crc2_dir;

int ARCH_MAINDECL
main(int argc, char *argv[])
{
	char file1[256];
	char file2[256];
	int i;

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	fix_gcr = 0;
	gap_match_length = 7;
	cap_relax = 0;
	mode = 0;
	reduce_sync = 4;
	reduce_badgcr = 0;
	reduce_gap = 0;
	verbose = 1;
	cap_min_ignore = 0;

	fprintf(stdout,
		"\nnibscan - Commodore disk image scanner / comparator\n"
		AUTHOR VERSION "\n\n");

	/* we can do nothing with no switches */
	if (argc < 2)
		usage();

	/* clear heap buffers */
	memset(compressed_buffer, 0x00, sizeof(compressed_buffer));
	memset(file_buffer, 0x00, sizeof(file_buffer));
	memset(track_buffer, 0x00, sizeof(track_buffer));
	memset(track_buffer2, 0x00, sizeof(track_buffer2));

	/* default is to reduce sync */
	memset(reduce_map, REDUCE_SYNC, MAX_TRACKS_1541+1);

	while (--argc && (*(++argv)[0] == '-'))
		parseargs(argv);

	if (argc < 0)	usage();
	strcpy(file1, argv[0]);

	if (argc > 1)
	{
		mode = 1;	//compare
		strcpy(file2, argv[1]);
	}
	printf("\n");

	if (mode == 1) 	// compare images
	{
		if(!(load_image(file1, track_buffer, track_density, track_length))) exit(0);
		if(!(load_image(file2, track_buffer2, track_density2, track_length2))) exit(0);

		compare_disks();

		/* disk 1 */
		printf("\n1: %s\n", file1);

		crc_dir = crc_dir_track(track_buffer, track_length);
		printf("BAM/DIR CRC:\t\t\t0x%X\n", crc_dir);
		crc = crc_all_tracks(track_buffer, track_length);
		printf("Full CRC:\t\t\t0x%X\n", crc);

		memset(md5_dir_hash_result, 0 , sizeof(md5_dir_hash_result));
		md5_dir_track(track_buffer, track_length, md5_dir_hash_result);
		printf("BAM/DIR MD5:\t\t\t0x");
		for (i = 0; i < 16; i++)
		 	printf ("%02x", md5_dir_hash_result[i]);
		printf("\n");

		memset(md5_hash_result, 0 , sizeof(md5_hash_result));
		md5_all_tracks(track_buffer, track_length, md5_hash_result);
		printf("Full MD5:\t\t\t0x");
		for (i = 0; i < 16; i++)
			printf ("%02x", md5_hash_result[i]);
		printf("\n");

		/* disk 2 */
		printf("\n2: %s\n", file2);
		crc2_dir = crc_dir_track(track_buffer2, track_length2);
		printf("BAM/DIR CRC:\t\t\t0x%X\n", crc2_dir);
		crc2 = crc_all_tracks(track_buffer2, track_length2);
		printf("Full CRC:\t\t\t0x%X\n", crc2);

		memset(md5_dir_hash_result2, 0 , sizeof(md5_dir_hash_result2));
		md5_dir_track(track_buffer2, track_length2, md5_dir_hash_result2);
		printf("BAM/DIR MD5:\t\t\t0x");
		for (i = 0; i < 16; i++)
		 	printf ("%02x", md5_dir_hash_result2[i]);
		printf("\n");

		memset(md5_hash_result2, 0 , sizeof(md5_hash_result2));
		md5_all_tracks(track_buffer2, track_length2, md5_hash_result2);
		printf("Full MD5:\t\t\t0x");
		for (i = 0; i < 16; i++)
			printf ("%02x", md5_hash_result2[i]);
		printf("\n\n");

		/* compare summary */
		if(crc_dir == crc2_dir)
			printf("BAM/DIR CRC matches.\n");
		else
			printf("BAM/DIR CRC does not match.\n");

		if( memcmp(md5_dir_hash_result, md5_dir_hash_result2, 16 ) == 0 )
			printf("BAM/DIR MD5 matches.\n");
		else
			printf("BAM/DIR MD5 does not match.\n");

		if(crc == crc2)
			printf("All decodable sectors have CRC matches.\n");
		else
			printf("All decodable sectors do not have CRC matches.\n");

		if( memcmp(md5_hash_result, md5_hash_result2, 16 ) == 0 )
			printf("All decodable sectors have MD5 matches.\n");
		else
			printf("All decodable sectors do not have MD5 matches.\n");
	}
	else 	// just scan for errors, etc.
	{
		if(!load_image(file1, track_buffer, track_density, track_length)) exit(0);

		scandisk();

		printf("\n%s\n", file1);

		crc = crc_dir_track(track_buffer, track_length);
		printf("BAM/DIR CRC:\t0x%X\n", crc);
		crc = crc_all_tracks(track_buffer, track_length);
		printf("Full CRC:\t0x%X\n", crc);

		memset(md5_hash_result, 0 , sizeof(md5_hash_result));
		md5_dir_track(track_buffer, track_length, md5_hash_result);
		printf("BAM/DIR MD5:\t0x");
		for (i = 0; i < 16; i++)
		 	printf ("%02x", md5_hash_result[i]);
		printf("\n");

		memset(md5_hash_result, 0 , sizeof(md5_hash_result));
		md5_all_tracks(track_buffer, track_length, md5_hash_result);
		printf("Full MD5:\t0x");
		for (i = 0; i < 16; i++)
			printf ("%02x", md5_hash_result[i]);
		printf("\n");
	}

	exit(0);
}

int load_image(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	if (compare_extension(filename, "D64"))
	{
		if(!(read_d64(filename, track_buffer, track_density, track_length))) return 0;
	}
	else if (compare_extension(filename, "G64"))
	{
		if(!(read_g64(filename, track_buffer, track_density, track_length))) return 0;
		if(sync_align_buffer) sync_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(filename, "NBZ"))
	{
		printf("Uncompressing NBZ...\n");
		if(!(file_buffer_size = load_file(filename, compressed_buffer))) return 0;
		if(!(file_buffer_size = LZ_Uncompress(compressed_buffer, file_buffer, file_buffer_size))) return 0;
		if(!(read_nib(file_buffer, file_buffer_size, track_buffer, track_density, track_length))) return 0;
		align_tracks(track_buffer, track_density, track_length, track_alignment);
		if(fattrack!=99) search_fat_tracks(track_buffer, track_density, track_length);
	}
	else if (compare_extension(filename, "NIB"))
	{
		if(!(file_buffer_size = load_file(filename, file_buffer))) return 0;
		if(!(read_nib(file_buffer, file_buffer_size, track_buffer, track_density, track_length))) return 0;
		align_tracks(track_buffer, track_density, track_length, track_alignment);
		if(fattrack!=99) search_fat_tracks(track_buffer, track_density, track_length);
	}
	else if (compare_extension(filename, "NB2"))
	{
		if(!(read_nb2(filename, track_buffer, track_density, track_length))) return 0;
		align_tracks(track_buffer, track_density, track_length, track_alignment);
		if(fattrack!=99) search_fat_tracks(track_buffer, track_density, track_length);
	}
	else
	{
		printf("Unknown image type = %s!\n", filename);
		return 0;
	}
	return 1;
}

int
compare_disks(void)
{
	int track;
	size_t numtracks = 0;
	size_t numsecs = 0;
	size_t gcr_match = 0;
	size_t sec_match = 0;
	size_t dens_mismatch = 0;
	size_t gcr_total = 0;
	size_t sec_total = 0;
	size_t trk_total = 0;
	size_t errors_d1 = 0, errors_d2 = 0;
	size_t gcr_percentage;
	char gcr_mismatches[256];
	char sec_mismatches[256];
	char gcr_matches[256];
	char sec_matches[256];
	char dens_mismatches[256];
	char tmpstr[16];
	char errorstring[0x1000];
	BYTE id[3], id2[3], cid[3], cid2[3];

	gcr_mismatches[0] = '\0';
	sec_mismatches[0] = '\0';
	gcr_matches[0] = '\0';
	sec_matches[0] = '\0';
	dens_mismatches[0] = '\0';

	/* ignore halftracks in compare */
	track_inc = 2;

	// extract disk id's from track 18
	memset(id, 0, 3);
	extract_id(track_buffer + (36 * NIB_TRACK_LENGTH), id);
	memset(id2, 0, 3);
	extract_id(track_buffer2 + (36 * NIB_TRACK_LENGTH), id2);

	memset(cid, 0, 3);
	extract_cosmetic_id(track_buffer + (36 * NIB_TRACK_LENGTH), cid);
	memset(cid2, 0, 3);
	extract_cosmetic_id(track_buffer2 + (36 * NIB_TRACK_LENGTH), cid2);

	if(waitkey) getchar();
	printf("\nComparing...\n");

	for (track = start_track; track <= end_track; track ++)
	{
		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]))
		{
			track_length[track] = 0;
			//printf("1 - UNFORMATTED!\n");
			continue;
		}

		if(!check_formatted(track_buffer2 + (track * NIB_TRACK_LENGTH), track_length2[track]))
		{
			track_length2[track] = 0;
			//printf("2 - UNFORMATTED!\n");
			continue;
		}

		printf("%4.1f, Disk 1: (%d) %d\n",
		 	(float)track/2, track_density[track]&3, track_length[track]);

		printf("%4.1f, Disk 2: (%d) %d\n",
		 	(float)track/2, track_density2[track]&3, track_length2[track]);

		numtracks++;

		// check for gcr match (unlikely)
		gcr_match =
		  compare_tracks(
			track_buffer + (track * NIB_TRACK_LENGTH),
			track_buffer2 + (track * NIB_TRACK_LENGTH),
			track_length[track],
			track_length2[track],
			0,
			errorstring);

		printf("%s", errorstring);

		if(gcr_match)
		{
			gcr_percentage = (gcr_match*100)/track_length[track];

			if (gcr_percentage >= 98)
			{
				gcr_total++;
				printf("\n[*>%d%% GCR MATCH*]\n", (gcr_match*100)/track_length[track]);
				sprintf(tmpstr, "%d,", track/2);
				strcat(gcr_matches, tmpstr);
			}
			else
			{
				printf("\n[*>%d%% GCR MATCH*]\n", (gcr_match*100)/track_length[track]);
				sprintf(tmpstr, "%d,", track/2);
				strcat(gcr_mismatches, tmpstr);
			}
		}

		if(track/2 <= 35)
		{
			errors_d1 += check_errors(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], track, id, errorstring);
			errors_d2 += check_errors(track_buffer2 + (NIB_TRACK_LENGTH * track), track_length2[track], track, id2, errorstring);
		}

		/* check for DOS sector matches */
		if(track/2 <= 35)
		{
			sec_match = compare_sectors(
										track_buffer + (track * NIB_TRACK_LENGTH),
										track_buffer2 + (track * NIB_TRACK_LENGTH),
										track_length[track],
										track_length2[track],
										id,
										id2,
										track,
										errorstring
										);

			printf("%s", errorstring);

			sec_total += sec_match;
			numsecs += sector_map[track/2];

			if (sec_match == sector_map[track/2])
			{
				trk_total++;
				printf("[*Data MATCH*]\n");
				sprintf(tmpstr, "%d,", track / 2);
				strcat(sec_matches, tmpstr);
			}
			else
			{
				printf("[*Data MISmatch*]\n");
				sprintf(tmpstr, "%d,", track / 2);
				strcat(sec_mismatches, tmpstr);
			}
		}

		if(track_density[track] != track_density2[track])
		{
			printf("[Densities do not match: %d != %d]\n", track_density[track], track_density2[track]);
			dens_mismatch++;
			sprintf(tmpstr, "%d,", track / 2);
			strcat(dens_mismatches, tmpstr);
		}
		printf("\n");

		if((!sec_match) || (track_density[track] != track_density2[track]))
			if( waitkey) getchar();
	}

	printf("\n---------------------------------------------------------------------\n");
	printf("%d/%d tracks had at least 98%% GCR match\n", gcr_total, numtracks);
	//printf("Matches (%s)\n", gcr_matches);
	//printf("Mismatches (%s)\n", gcr_mismatches);
	//printf("\n");
	printf("%d/%d of likely formatted tracks matched all sector data\n", trk_total, numtracks);
	//printf("Matches (%s)\n", sec_matches);
	//printf("Mismatches (%s)\n", sec_mismatches);
	//printf("\n");
	printf("%d/%d total sectors (or errors) matched (%d mismatched)\n", sec_total, numsecs, numsecs-sec_total);
	printf("CBM DOS errors (d1/%d - d2/%d)\n",errors_d1, errors_d2);
	printf("%d tracks had mismatched densities (%s)\n", dens_mismatch, dens_mismatches);

	if(!(id[0]==id2[0] && id[1]==id2[1]))
		printf("\nFormat ID's do not match!:\t(%s != %s)", id, id2);
	else
		printf("\nFormat ID's match:\t\t(%s = %s)", id, id2);

	if(!(cid[0]==cid2[0] && cid[1]==cid2[1]))
		printf("\nCosmetic ID's do not match:\t(%s != %s)\n", cid, cid2);
	else
		printf("\nCosmetic ID's match:\t\t(%s = %s)\n", cid, cid2);

	printf("---------------------------------------------------------------------\n");

	return 1;
}

int
scandisk(void)
{
	BYTE id[3], cosmetic_id[3];
	int track = 0;
	int totalfat = 0;
	int totalrl = 0;
	int added_sync;
	size_t totalgcr = 0;
	int total_wrong_density = 0;
	size_t empty = 0, temp_empty = 0;
	size_t errors = 0, temp_errors = 0;
	int defdensity;
	char errorstring[0x1000];
	char testfilename[16];
	FILE *trkout;

	// clear buffers
	memset(badgcr_tracks, 0, sizeof(badgcr_tracks));
	memset(fat_tracks, 0, sizeof(fat_tracks));
	memset(rapidlok_tracks, 0, sizeof(rapidlok_tracks));
	errorstring[0] = '\0';

	printf("\nScanning...\n");

	// extract disk id from track 18
	memset(id, 0, 3);
	extract_id(track_buffer + (36 * NIB_TRACK_LENGTH), id);
	printf("\ndisk id: %s\n", id);

	// collect and print "cosmetic" disk id for comparison
	memset(cosmetic_id, 0, 3);
	extract_cosmetic_id(track_buffer + (36 * NIB_TRACK_LENGTH), cosmetic_id);
	printf("cosmetic disk id: %s\n", cosmetic_id);

	if(waitkey) getchar();

	// check each track for various things
	for (track = start_track; track <= end_track; track ++)
	{
		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]))
		{
			//printf(":UNFORMATTED\n");
			continue;
		}
		else
			printf("%4.1f: %d",(float) track/2, track_length[track]);

		if (track_length[track] > 0)
		{
			track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH),
				track_density[track]&3, track_length[track]);

			printf(" (density:%d", track_density[track]&3);

			if (track_density[track] & BM_NO_SYNC)
				printf(":NOSYNC");
			else if (track_density[track] & BM_FF_TRACK)
				printf(":KILLER");

			// establish default density and warn
			defdensity = speed_map[track/2];

			if ((track_density[track] & 3) != defdensity)
			{
				printf("!=%d?) ", defdensity);
				if(track < 36*2) total_wrong_density++;
				if(waitkey) getchar();
			}
			else
				printf(") ");

			if(increase_sync)
			{
				added_sync = lengthen_sync(track_buffer + (NIB_TRACK_LENGTH * track),
					track_length[track], NIB_TRACK_LENGTH);

				printf("[sync:%d] ", added_sync);
				track_length[track] += added_sync;
			}

			// detect bad GCR '000' bits
			badgcr_tracks[track] =
			  check_bad_gcr(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track]);

			if (badgcr_tracks[track])
			{
				//printf("weak:%d ", badgcr_tracks[track]);
				totalgcr += badgcr_tracks[track];
			}

			/* check for rapidlok track
			rapidlok_tracks[track] = check_rapidlok(track);

			if (rapidlok_tracks[track]) totalrl++;
			if ((totalrl) && (track == 72))
			{
				printf("RAPIDLOK KEYTRACK ");
				rapidlok_tracks[track] = 1;
			}
			*/

			/* check for FAT track */
			if(fattrack!=99)
			{
				if (track < end_track - track_inc)
				{
					fat_tracks[track] = check_fat(track);
					if (fat_tracks[track]) totalfat++;
				}
			}

			/* check for regular disk errors
				"second half" of fat track will always have header
				errors since it's encoded for the wrong track number.
				rapidlok tracks are not standard gcr
				tracks above 35 are always CBM errors
			*/
			if(track/2 <= 35)
				temp_errors = check_errors(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], track, id, errorstring);
			else /* everything is a CBM error above track 35 */
				temp_errors = 0;

			if (temp_errors)
			{
				errors += temp_errors;
				printf("%s", errorstring);
				if(waitkey) getchar();
			}

			temp_empty = check_empty(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], track, id, errorstring);
			if (temp_empty)
			{
				empty += temp_empty;
				if(verbose>1) printf(" %s", errorstring);
			}

			if (verbose>1)
			{
					dump_headers(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track]);
					raw_track_info(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track]);
			}
		}
		else
		{
			printf("(%d", track_density[track]&3);
			printf(":UNFORMATTED");
		}
		printf("\n");

		// process and dump to disk for manual compare
		//track_length[track] = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		sprintf(testfilename, "raw/tr%.1fd%d", (float) track/2, (track_density[track] & 3));
		if(NULL != (trkout = fopen(testfilename, "w")))
		{
			fwrite(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track], 1, trkout);
			fclose(trkout);
		}
	}
	printf("\n---------------------------------------------------------------------\n");
	printf("%d unrecognized sectors (CBM disk errors) detected\n", errors);
	printf("%d known empty sectors detected\n", empty);
	printf("%d bad GCR bytes detected\n", totalgcr);
	printf("%d fat tracks detected\n", totalfat);
	printf("%d rapidlok tracks detected\n", totalrl);
	printf("%d tracks with non-standard density\n", total_wrong_density);
	return 1;
}

int
dump_headers(BYTE * gcrdata, size_t length)
{
	BYTE header[10];
	BYTE *gcr_ptr, *gcr_end;

	gcr_ptr = gcrdata;
	gcr_end = gcrdata + length;

	do
	{
		if (!find_sync(&gcr_ptr, gcr_end))
			return 0;

		convert_4bytes_from_GCR(gcr_ptr, header);
		convert_4bytes_from_GCR(gcr_ptr + 5, header + 4);

		if(header[0] == 0x08) // only parse headers
			printf("\n%.2x %.2x %.2x %.2x = typ:%.2x -- blh:%.2x -- trk:%d -- sec:%d -- id:%c%c",
				*gcr_ptr, *(gcr_ptr+1), *(gcr_ptr+2), *(gcr_ptr+3), header[0], header[1], header[3], header[2], header[5], header[4]);
		else // data block should follow
			printf("\n%.2x %.2x %.2x %.2x = typ:%.2x",
				*gcr_ptr, *(gcr_ptr+1), *(gcr_ptr+2), *(gcr_ptr+3), header[0]);

	} while (gcr_ptr < (gcr_end - 10));

	printf("\n");

	return 1;
}


int
raw_track_info(BYTE * gcrdata, size_t length)
{
	size_t sync_cnt = 0;
	size_t sync_len[NIB_TRACK_LENGTH];
	/*
	int gap_cnt = 0;
	int gap_len[NIB_TRACK_LENGTH];
	*/
	size_t bad_cnt = 0;
	size_t bad_len[NIB_TRACK_LENGTH];
	size_t i, locked;

	memset(sync_len, 0, sizeof(sync_len));
	/* memset(gap_len, 0, sizeof(gap_len)); */
	memset(bad_len, 0, sizeof(bad_len));

	/* count syncs/lengths */
	for (locked = 0, i = 0; i < length - 1; i++)
	{
		if (locked)
		{
			if (gcrdata[i] == 0xff)
				sync_len[sync_cnt]++;
			else
				locked = 0;
		}
		//else if (gcrdata[i] == 0xff) /* not full sync, only last 8 bits */
		else if(((gcrdata[i] & 0x03) == 0x03) && (gcrdata[i+1] == 0xff))
		{
			locked = 1;
			sync_cnt++;
			sync_len[sync_cnt] = 1;
		}
	}

	printf("\nSYNCS:%d (", sync_cnt);
	for (i = 1; i <= sync_cnt; i++)
		printf("%d-", sync_len[i]);
	printf(")");

	/* count gaps/lengths - this code is innacurate, since gaps are of course not always 0x55 - they rarely are */
	/*
	for (locked = 0, i = 0; i < length - 1; i++)
	{
		if (locked)
		{
			if (gcrdata[i] == 0x55)
				gap_len[gap_cnt]++;
			else
				locked = 0;
		}
		else if ((gcrdata[i] == 0x55) && (gcrdata[i + 1] == 0x55))
		{
			locked = 1;
			gap_cnt++;
			gap_len[gap_cnt] = 2;
		}
	}

	printf("\nGAPS :%d (", gap_cnt);
	for (i = 1; i <= gap_cnt; i++)
		printf("%d-", gap_len[i]);
	printf(")");
	*/

	/* count bad gcr lengths */
	for (locked = 0, i = 0; i < length - 1; i++)
	{
		if (locked)
		{
			if (is_bad_gcr(gcrdata, length, i))
				bad_len[bad_cnt]++;
			else
				locked = 0;
		}
		else if (is_bad_gcr(gcrdata, length, i))
		{
			locked = 1;
			bad_cnt++;
			bad_len[bad_cnt] = 1;
		}
	}

	printf("\nBADGCR:%d (", bad_cnt);
	for (i = 1; i <= bad_cnt; i++)
		printf("%d-", bad_len[i]);
	printf(")");

	return 1;
}

size_t check_fat(int track)
{
	size_t diff = 0;
	char errorstring[0x1000];

	if (track_length[track] > 0 && track_length[track+2] > 0 && track_length[track] != 8192 && track_length[track+2] != 8192)
	{
		diff = compare_tracks(
		  track_buffer + (track * NIB_TRACK_LENGTH),
		  track_buffer + ((track+2) * NIB_TRACK_LENGTH),
		  track_length[track],
		  track_length[track+2], 1, errorstring);

		if(verbose>1) printf("%s",errorstring);

		if (diff<=10)
		{
			printf("*FAT diff=%d*",(int)diff);
			return 1;
		}
		else if (diff<34) /* 34 happens on empty formatted disks */
		{
			printf("*Possible FAT diff=%d*",(int)diff);
			return 1;
		}
		else
			if(verbose>1) printf("diff=%d",(int)diff);
	}
	return 0;
}

/*
	tries to detect and fixup rapidlok track, as the gcr routines
	don't assemble them quite right.
	this is innaccurate!
*/

size_t check_rapidlok(int track)
{
	size_t i;
	size_t end_key = 0;
	size_t end_sync = 0;
	size_t synclen = 0;
	size_t keylen = 0;		// extra sector with # of 0x7b
	size_t tlength = track_length[track];
	BYTE *gcrdata = track_buffer + (track * NIB_TRACK_LENGTH);

	// extra sector is at the end.
	// count the extra-sector (key) bytes.
	for (i = 0; i < 200; i++)
	{
		if (gcrdata[tlength - i] == 0x7b)
		{
			keylen++;
			if (end_key)
				end_key = tlength - i;	// move marked end of key bytes
		}
		else if (keylen)
			break;
	}

	if (gcrdata[tlength - i] != 0xff)
	{
		keylen++;
		end_key++;
	}

	// only rapidlok tracks contain lots of these at start
	if (keylen < 0x8)
		return (0);

	for (i = end_key + 1; i < end_key + 0x100; i++)
	{
		if (gcrdata[i] == 0xff)
		{
			synclen++;
			end_sync = i + 1;	// mark end of sync
		}
		else if (synclen)
			break;
	}

	printf("RAPIDLOK! ");
	printf("key:%d, sync:%d...", keylen, synclen);

#if 0
	// recreate key sector
	memset(extra_sector, 0xff, 0x14);
	memset(extra_sector + 0x14, 0x7b, keylen);

	// create initial sync, then copy all sector data
	// if directory track no fancy stuff
	if(halftrack != 0x24)
	{
		memset(gcrdata, 0xff, 0x29);
		memcpy(gcrdata + 0x29, gcrdata + end_sync, length - end_sync);
		memcpy(gcrdata + 0x29 + (length - end_sync), extra_sector,
		  keylen + 0x14);
	}
	else
	{
		memcpy(gcrdata, gcrdata + end_key, length - end_key);
		memcpy(gcrdata + (length - end_key), extra_sector, keylen + 0x14);
	}
#endif // 0

	return keylen;
}

void
usage(void)
{
	printf("usage: nibscan [options] <filename1> [filename2]\n\n");
	switchusage();
	exit(1);
}

