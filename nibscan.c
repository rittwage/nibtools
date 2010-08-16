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
#include "md5.h"

int _dowildcard = 1;

int start_track, end_track, track_inc;
int imagetype, mode;
int align, force_align;
char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };

int load_image(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length);
int compare_disks(void);
int scandisk(void);
int raw_track_info(BYTE *gcrdata, size_t length);
size_t check_fat(int track);
size_t check_rapidlok(int track);

BYTE *track_buffer;
BYTE *track_buffer2;
size_t track_length[MAX_HALFTRACKS_1541 + 1];
size_t track_length2[MAX_HALFTRACKS_1541 + 1];
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_density2[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment2[MAX_HALFTRACKS_1541 + 1];

size_t fat_tracks[MAX_HALFTRACKS_1541 + 1];
size_t rapidlok_tracks[MAX_HALFTRACKS_1541 + 1];
size_t badgcr_tracks[MAX_HALFTRACKS_1541 + 1];

int fix_gcr;
int reduce_sync;
int reduce_badgcr;
int reduce_gap;
int waitkey = 0;
int gap_match_length;
int cap_relax;
int verbose;
int rpm_real = 0;
int drive;
int auto_capacity_adjust;
int skew;
int align_disk;
int ihs;
int unformat_passes;
int capacity_margin;
int align_delay;
int cap_min_ignore;
BYTE fillbyte = 0x55;

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
	fix_gcr = 1;
	gap_match_length = 7;
	cap_relax = 0;
	mode = 0;
	reduce_sync = 3;
	reduce_badgcr = 0;
	reduce_gap = 0;
	rpm_real = 0;
	verbose = 0;
	cap_min_ignore = 0;

	/* we can do nothing with no switches */
	if (argc < 2)	usage();

	track_buffer = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH);
	if(!track_buffer)
	{
		printf("could not allocate buffer memory\n");
		exit(0);
	}

	track_buffer2 = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH);
	if(!track_buffer2)
	{
		printf("could not allocate buffer memory\n");
		free(track_buffer);
		exit(0);
	}

	/* default is to reduce sync */
	memset(reduce_map, REDUCE_SYNC, MAX_TRACKS_1541 + 1);

	while (--argc && (*(++argv)[0] == '-'))
		parseargs(argv);

	printf("\nnibscan - Commodore disk image scanner / comparator\n"
	  "(C) 2004-2010 Peter Rittwage\nC64 Preservation Project\nhttp://c64preservation.com\n" "Version " VERSION "\n\n");

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
		if(!load_image(file1, track_buffer, track_density, track_length))
			exit(0);
		if(!load_image(file2,  track_buffer2, track_density2, track_length2))
			exit(0);

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
		if(!load_image(file1, track_buffer, track_density, track_length))
			exit(0);

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

	free(track_buffer);
	free(track_buffer2);

	exit(0);
}

int
load_image(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	char command[256];
	char pathname[256];
	char *dotpos, *pathpos;
	int iszip = 0;
	int retval = 0;

	/* unzip image if possible */
	if (compare_extension(filename, "ZIP"))
	{
		printf("Unzipping image...\n");
		dotpos = strrchr(filename, '.');
		if (dotpos != NULL) *dotpos = '\0';

		/* try to detect pathname */
		strcpy(pathname, filename);
		pathpos = strrchr(pathname, '\\');
		if (pathpos != NULL)
			*pathpos = '\0';
		else //*nix
		{
			pathpos = strrchr(pathname, '/');
			if (pathpos != NULL)
				*pathpos = '\0';
		}

		sprintf(command, "unzip '%s.zip' -d '%s'", filename, pathname);
		system(command);
		iszip++;
	}

	if (compare_extension(filename, "D64"))
		retval = read_d64(filename, track_buffer, track_density, track_length);
	else if (compare_extension(filename, "G64"))
		retval = read_g64(filename, track_buffer, track_density, track_length);
	else if (compare_extension(filename, "NIB"))
	{
		retval = read_nib(filename, track_buffer, track_density, track_length);
		if(retval) align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(filename, "NB2"))
	{
		retval = read_nb2(filename, track_buffer, track_density, track_length);
		if(retval) align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else
		printf("Unknown image type = %s!\n", filename);

	if(iszip)
	{
		remove(filename);
		printf("Temporary file deleted (%s)\n", filename);
	}

	return retval;
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

	for (track = start_track; track <= end_track; track += track_inc)
	{
		printf("%4.1f, Disk 1: (%d) %zu\n",
		 	(float)track/2, track_density[track]&3, track_length[track]);

		printf("%4.1f, Disk 2: (%d) %zu\n",
		 	(float)track/2, track_density2[track]&3, track_length2[track]);

		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]))
		{
			track_length[track] = 0;
			printf("1 - UNFORMATTED!\n");
		}

		if(!check_formatted(track_buffer2 + (track * NIB_TRACK_LENGTH), track_length[track]))
		{
			track_length2[track] = 0;
			printf("2 - UNFORMATTED!\n");
		}

		if( ((track_length[track] > 0) && (track_length2[track] == 0)) ||
			((track_length[track] == 0) && (track_length2[track] > 0)) )
		{
			printf("[Track sizes do not match]\n");
			if(waitkey) getchar();
		}

		if ((track_length[track] > 0) || (track_length2[track] > 0))
		{
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

			gcr_percentage = (gcr_match*100)/track_length[track];

			if (gcr_percentage >= 95)
			{
				gcr_total++;
				printf("\n[*%zu%% GCR MATCH*]\n", (gcr_match*100)/track_length[track]);
				sprintf(tmpstr, "%d,", track / 2);
				strcat(gcr_matches, tmpstr);
			}
			else
			{
				printf("\n[*%zu%% GCR MATCH*]\n", (gcr_match*100)/track_length[track]);
				sprintf(tmpstr, "%d,", track/2);
				strcat(gcr_mismatches, tmpstr);
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
					printf("[*DATA MATCH*]\n");
					sprintf(tmpstr, "%d,", track / 2);
					strcat(sec_matches, tmpstr);
				}
				else
				{
					printf("[*NO DATA MATCH*]\n");
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
	}

	printf("\n---------------------------------------------------------------------\n");
	printf("%zu/%zu tracks had at least 95%% GCR match\n", gcr_total, numtracks);
	//printf("Matches (%s)\n", gcr_matches);
	//printf("Mismatches (%s)\n", gcr_mismatches);
	//printf("\n");
	printf("%zu/%zu of likely formatted tracks matched all sector data\n", trk_total, numtracks);
	//printf("Matches (%s)\n", sec_matches);
	//printf("Mismatches (%s)\n", sec_mismatches);
	//printf("\n");
	printf("%zu/%zu total sectors (or errors) matched (%zu mismatched)\n", sec_total, numsecs, numsecs-sec_total);
	printf("CBM DOS errors (d1/%zu - d2/%zu)\n",errors_d1, errors_d2);
	printf("%zu tracks had mismatched densities (%s)\n", dens_mismatch, dens_mismatches);

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
	for (track = start_track; track <= end_track; track += track_inc)
	{
		//printf("-------------------------------------------------\n");
		printf("%4.1f: ", (float) track/2);

		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH), track_length[track]))
			printf("UNFORMATTED!");
		else
			printf("%zu", track_length[track]);

		if (track_length[track] > 0)
		{
			track_density[track] = check_sync_flags(track_buffer + (track * NIB_TRACK_LENGTH),
				track_density[track]&3, track_length[track]);

			printf("(%d", track_density[track]&3);

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

			// detect bad GCR '000' bits
			if (fix_gcr)
			{
				badgcr_tracks[track] =
				  check_bad_gcr(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track]);

				if (badgcr_tracks[track])
				{
					printf("badgcr:%zu ", badgcr_tracks[track]);
					totalgcr += badgcr_tracks[track];
				}
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

			/* check for FAT track
			if (track < end_track - track_inc)
			{
				fat_tracks[track] = check_fat(track);
				if (fat_tracks[track]) totalfat++;
			}
			*/

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
				printf("\n%s", errorstring);
				if(waitkey) getchar();
			}

			temp_empty = check_empty(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], track, id, errorstring);
			if (temp_empty)
			{
				empty += temp_empty;
				if(verbose>1) printf("\n%s", errorstring);
			}

			if (verbose>1)
					raw_track_info(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track]);
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
	printf("%zu unrecognized sectors (CBM disk errors) detected\n", errors);
	printf("%zu known empty sectors detected\n", empty);
	printf("%zu bad GCR bytes detected\n", totalgcr);
	printf("%d fat tracks detected\n", totalfat);
	printf("%d rapidlok tracks detected\n", totalrl);
	printf("%d tracks with non-standard density\n", total_wrong_density);
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

	printf("\nSYNCS:%zu (", sync_cnt);
	for (i = 1; i <= sync_cnt; i++)
		printf("%zu-", sync_len[i]);
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

	printf("\nBADGCR:%zu (", bad_cnt);
	for (i = 1; i <= bad_cnt; i++)
		printf("%zu-", bad_len[i]);
	printf(")");

	return 1;
}

size_t check_fat(int track)
{
	size_t fat = 0;
	char errorstring[0x1000];

	if (track_length[track] > 0 && track_length[track+2] > 0)
	{
		fat = compare_tracks(track_buffer + (track * NIB_TRACK_LENGTH),
		  track_buffer + ((track+2) * NIB_TRACK_LENGTH), track_length[track],
		  track_length[track+2], 1, errorstring);
	}

	if (fat) printf("*FAT* ");
	return fat;
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
	printf("key:%zu, sync:%zu...", keylen, synclen);

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
	printf("usage: nibscan [options] <filename1> [filename2]\n"
		"\nsupported file extensions:\n"
		"NIB, NB2, D64, G64\n"
		"\noptions:\n"
		" -a[x]: Force alternative track alignments (advanced users only)\n"
		" -p[x]: Custom protection handlers (advanced users only)\n"
		" -g: Enable gap reduction\n"
		" -f: Disable automatic bad GCR detection\n"
		" -0: Enable bad GCR run reduction\n"
		" -g: Enable gap reduction\n"
		" -r: Disable automatic sync reduction\n"
		" -G: Manual gap match length\n"
		" -v: Verbose (output more detailed track data)\n");
	exit(1);
}

