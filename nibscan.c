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
#include "version.h"
#include "md5.h"

int start_track, end_track, track_inc;
int imagetype, mode;
int align, force_align;
char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };

int load_image(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length);
int compare_disks(void);
int scandisk(void);
int raw_track_info(BYTE *gcrdata, int length, char *outputstring);
int check_fat(int track);
int check_rapidlok(int track);

BYTE *track_buffer;
BYTE *track_buffer2;
int track_length[MAX_HALFTRACKS_1541 + 1];
int track_length2[MAX_HALFTRACKS_1541 + 1];
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_density2[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment2[MAX_HALFTRACKS_1541 + 1];

int fat_tracks[MAX_HALFTRACKS_1541 + 1];
int rapidlok_tracks[MAX_HALFTRACKS_1541 + 1];
int badgcr_tracks[MAX_HALFTRACKS_1541 + 1];

int fix_gcr;
int reduce_sync;
int reduce_badgcr;
int reduce_gap;
int waitkey = 0;
int advanced_info;
int gap_match_length;
int cap_min_ignore;
int verbose = 0;

unsigned char md5_hash_result[16];
unsigned char md5_hash_result2[16];
int crc, crc2;

void
usage(void)
{
	fprintf(stderr, "usage: nibscan [options] <filename1> [filename2]\n"
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
		" -w: Wait for a keypress upon errors or compare differences\n"
		" -v: Verbose (output more detailed track data)\n");
	exit(1);
}

int ARCH_MAINDECL
main(int argc, char *argv[])
{
	char file1[256];
	char file2[256];

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	fix_gcr = 1;
	gap_match_length = 7;
	cap_min_ignore = 0;
	mode = 0;
	reduce_sync = 1;
	reduce_badgcr = 0;
	reduce_gap = 0;

	fprintf(stdout,
	  "\nnibscan - Commodore disk image scanner / comparator\n"
	  "(C) C64 Preservation Project\nhttp://c64preservation.com\n" "Version " VERSION "\n\n");

	/* we can do nothing with no switches */
	if (argc < 2)	usage();

	if(!(track_buffer = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate buffer memory\n");
		exit(0);
	}

	if(!(track_buffer2 = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate buffer memory\n");
		free(track_buffer);
		exit(0);
	}

	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{

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

		case 'w':
			printf("* Wait for keypress disabled\n");
			waitkey = 1;
			break;

		case 'v':
			printf("* Verbose mode (more detailed track info)\n");
			advanced_info = 1;
			verbose = 1;
			break;

		case 'G':
			if (!(*argv)[2])
				usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'f':
			printf("* Bad GCR correction/simulation disabled\n");
			fix_gcr = 0;
			break;

		case 'p':
			// custom protection handling
			printf("* Custom copy protection handler: ");
			if ((*argv)[2] == 'x')
			{
				printf("V-MAX!\n");
				force_align = ALIGN_VMAX;
				fix_gcr= 0;
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
				//reduce_sync = 0;
				//reduce_badgcr = 1;
			}
			else if ((*argv)[2] == 'v')
			{
				printf("VORPAL (NEWER)\n");
				force_align = ALIGN_AUTOGAP;
			}
			else if ((*argv)[2] == 'r')
			{
				printf("RAPIDLOK\n");
				//reduce_sync = 1;
				//reduce_badgcr = 1;
			}
			else
				printf("Unknown protection handler\n");
			break;

		case 'a':
			// custom alignment handling
			printf("* Custom alignment: ");
			if ((*argv)[2] == '0')
			{
				printf("sector 0\n");
				force_align = ALIGN_SEC0;
			}
			else if ((*argv)[2] == 'w')
			{
				printf("longest bad GCR run\n");
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
			else if ((*argv)[2] == 'n')
			{
				printf("raw (no alignment, use NIB start)\n");
				force_align = ALIGN_RAW;
			}
			else
				printf("Unknown alignment parameter\n");
			break;

		case 'm':
			printf("* Minimum capacity ignore on\n");
			cap_min_ignore = 1;
			break;

		default:
			break;
		}
	}

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

		printf("\nfile #1: %s\n", file1);
		printf("file #2: %s\n", file2);

		compare_disks();

		printf("\nfile #1: %s\n", file1);
		printf("file #2: %s\n", file2);
	}
	else 	// just scan for errors, etc.
	{
		if(!load_image(file1, track_buffer, track_density, track_length))
			exit(0);

		printf("\n1: %s\n", file1);
		scandisk();
	}

	/* CRC32 */
	printf("\n");

	crc = crc_dir_track(track_buffer, track_length);
	if(mode==1)
	{
		crc2 = crc_dir_track(track_buffer2, track_length2);
		if( crc == crc2 )
			printf( "*MATCH*\n" );
		else
			printf("*no match*\n");
	}

	printf("\n");

	crc = crc_all_tracks(track_buffer, track_length);
	if(mode==1)
	{
		crc2 = crc_all_tracks(track_buffer2, track_length2);
		if( crc == crc2 )
			printf( "*MATCH*\n" );
		else
			printf("*no match*\n");
	}

	/* MD5 */
	printf("\n");

	md5_dir_track(track_buffer, track_length, md5_hash_result);
	if(mode==1)
	{
		md5_dir_track(track_buffer2, track_length2, md5_hash_result2);
		if( memcmp(md5_hash_result, md5_hash_result2, 16 ) == 0 )
			printf( "*MATCH*\n" );
		else
			printf("*no match*\n");
	}

	printf("\n");

	md5_all_tracks(track_buffer, track_length, md5_hash_result);
	if(mode==1)
	{
		md5_all_tracks(track_buffer2, track_length2, md5_hash_result2);
		if( memcmp(md5_hash_result, md5_hash_result2, 16 ) == 0 )
			printf( "*MATCH*\n" );
		else
			printf("*no match*\n");
	}

	free(track_buffer);
	free(track_buffer2);

	exit(0);
}

int
load_image(char *filename, BYTE *track_buffer, BYTE *track_density, int *track_length)
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

		sprintf(command, "unzip %s.zip -d %s", filename, pathname);
		system(command);
		iszip++;
	}

	if (compare_extension(filename, "D64"))
		retval = read_d64(filename, track_buffer, track_density, track_length);
	else if (compare_extension(filename, "G64"))
		retval = read_g64(filename, track_buffer, track_density, track_length);
	else if (compare_extension(filename, "NIB"))
	{
		retval = read_nib(filename, track_buffer, track_density, track_length, track_alignment);
		if(retval) align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(filename, "NB2"))
	{
		retval = read_nb2(filename, track_buffer, track_density, track_length, track_alignment);
		if(retval) align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else
		printf("Unknown image type = %s!\n", filename);

	if(iszip)
	{
		unlink(filename);
		printf("Temporary file deleted (%s)\n", filename);
	}

	return retval;
}

int
compare_disks(void)
{
	int track;
	int numtracks = 0;
	int numsecs = 0;
	BYTE id[3];
	BYTE id2[3];
	char errorstring[0x1000];
	int gcr_match = 0;
	int sec_match = 0;
	int dens_mismatch = 0;
	int gcr_total = 0;
	int sec_total = 0;
	int trk_total = 0;
	int errors_d1 = 0, errors_d2 = 0;
	char gcr_mismatches[256];
	char sec_mismatches[256];
	char gcr_matches[256];
	char sec_matches[256];
	char dens_mismatches[256];
	char tmpstr[16];

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

	printf("\ndisk ID #1: %s\n", id);
	printf("disk ID #2: %s\n", id2);

	if(waitkey) getchar();

	for (track = start_track; track <= end_track; track += track_inc)
	{
		printf("Track %4.1f, Disk 1: (%d) %d\n",
		 	(float) track / 2, (track_density[track] & 3), track_length[track]);

		printf("Track %4.1f, Disk 2: (%d) %d\n",
		 	(float) track / 2, (track_density2[track] & 3), track_length2[track]);

		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH)))
		{
			track_length[track] = 0;
			printf("1 - UNFORMATTED!\n");
		}
		if(!check_formatted(track_buffer2 + (track * NIB_TRACK_LENGTH)))
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

			if (gcr_match)
			{
				gcr_total++;
				printf("\n[*GCR MATCH*]\n");
				sprintf(tmpstr, "%d,", track / 2);
				strcat(gcr_matches, tmpstr);
			}
			else
			{
				printf("\n[*NO GCR MATCH*]\n");
				sprintf(tmpstr, "%d,", track / 2);
				strcat(gcr_mismatches, tmpstr);
			}

			if(track/2 <= 35)
			{
				errors_d1 += check_errors(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], track, id, errorstring);
				errors_d2 += check_errors(track_buffer2 + (NIB_TRACK_LENGTH * track), track_length2[track], track, id2, errorstring);
			}

			/* check for DOS sector matches */
			if(1) //if(track/2 <= 35)
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
				numsecs += sector_map_1541[track/2];

				if (sec_match == sector_map_1541[track/2])
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
	printf("%d/%d tracks had a perfect GCR match\n", gcr_total, numtracks);
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
		printf("disk ID's do not match - (%s != %s)\n", id, id2);
	else
		printf("disk ID's matched - (%s = %s)\n", id, id2);

	return 1;
}

int
scandisk(void)
{
	BYTE id[3], cosmetic_id[3];
	int track = 0;
	int totalfat = 0;
	int totalrl = 0;
	int totalgcr = 0;
	int total_wrong_density = 0;
	int empty = 0, temp_empty = 0;
	int errors = 0, temp_errors = 0;
	int defdensity;
	int length;
	char errorstring[0x1000];
	char testfilename[16];
	FILE *trkout;

	track_inc = 2;

	// clear buffers
	memset(badgcr_tracks, 0, sizeof(badgcr_tracks));
	memset(fat_tracks, 0, sizeof(fat_tracks));
	memset(rapidlok_tracks, 0, sizeof(rapidlok_tracks));
	errorstring[0] = '\0';

	// extract disk id from track 18
	memset(id, 0, 3);
	extract_id(track_buffer + (36 * NIB_TRACK_LENGTH), id);
	printf("\ndisk id: %s\n", id);

	// collect and print "cosmetic" disk id for comparison
	memset(cosmetic_id, 0, 3);
	extract_cosmetic_id(track_buffer + (36 * NIB_TRACK_LENGTH), cosmetic_id);
	printf("cosmetic disk id: %s\n", cosmetic_id);

	if(waitkey) getchar();

	printf("Scanning...\n");

	// check each track for various things
	for (track = start_track; track <= end_track; track += track_inc)
	{
		printf("-------------------------------------------------\n");
		printf("Track %4.1f: ", (float) track/2);

		if(!check_formatted(track_buffer + (track * NIB_TRACK_LENGTH)))
		{
			track_length[track] = 0;
			printf("UNFORMATTED!\n");
		}
		else
			printf("%d", track_length[track]);

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
			defdensity = speed_map_1541[(track / 2) - 1];

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
				  check_bad_gcr(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], 1);

				if (badgcr_tracks[track])
				{
					printf("badgcr:%d ", badgcr_tracks[track]);
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

			/* check for FAT track */
			if (track < end_track - track_inc)
			{
				fat_tracks[track] = check_fat(track);
				if (fat_tracks[track]) totalfat++;
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
				printf("\n%s", errorstring);
				if(waitkey) getchar();
			}

			temp_empty = check_empty(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], track, id, errorstring);
			if (temp_empty)
			{
				empty += temp_empty;
				if(verbose) printf("\n%s", errorstring);
			}

			if (advanced_info)
					raw_track_info(track_buffer + (NIB_TRACK_LENGTH * track), track_length[track], errorstring);

			printf("\n");
		}

		// process and dump to disk for manual compare
		length = track_length[track];
		//length = compress_halftrack(track, track_buffer + (track * NIB_TRACK_LENGTH), track_density[track], track_length[track]);

		sprintf(testfilename, "raw/tr%.1fd%d", (float) track/2, (track_density[track] & 3));
		if(NULL != (trkout = fopen(testfilename, "w")))
		{
			fwrite(track_buffer + (track * NIB_TRACK_LENGTH), length, 1, trkout);
			fclose(trkout);
		}
	}
	printf("\n---------------------------------------------------------------------\n");
	printf("%d disk errors detected.\n", errors);
	printf("%d empty sectors detected.\n", empty);
	printf("%d bad GCR bytes detected.\n", totalgcr);
	printf("%d fat tracks detected.\n", totalfat);
	printf("%d rapidlok tracks detected.\n", totalrl);
	printf("%d tracks with non-standard density.\n", total_wrong_density);
	return 1;
}

int
raw_track_info(BYTE * gcrdata, int length, char *outputstring)
{
	int sync_cnt = 0;
	int sync_len[NIB_TRACK_LENGTH];
	/*
	int gap_cnt = 0;
	int gap_len[NIB_TRACK_LENGTH];
	*/
	int bad_cnt = 0;
	int bad_len[NIB_TRACK_LENGTH];
	int i, locked;

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
		else if (gcrdata[i] == 0xff) /* (((gcrdata[i] & 0x03) == 0x03) && (gcrdata[i + 1] == 0xff)) */ /* not full sync, only last 8 bits */
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

int check_fat(int track)
{
	int fat = 0;
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

int check_rapidlok(int track)
{
	int i;
	int end_key = 0;
	int end_sync = 0;
	int synclen = 0;
	int keylen = 0;		// extra sector with # of 0x7b
	int tlength = track_length[track];
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

