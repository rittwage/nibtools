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
#include <ctype.h>
#include <signal.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"
#include "prot.h"
#include "crc.h"
#include "md5.h"
//#include "bitshifter.c"

void parseargs(char *argv[])
{
	int count;
	double st, et;

	// parse arguments
	switch ((*argv)[1])
	{
		case '@':
			cbm_adapter = &(*argv)[2];
			printf("* Using OpenCBM adapter %s\n", cbm_adapter);
			break;

		case '$':
			sync_align_buffer = 1;
			printf("* Force sync align tracks\n");
			if((*argv)[2]=='$')
			{
				printf("* Don't force re-alignment\n");
				sync_align_buffer = 2;
			}
			break;

		case 'B':
			backwards = 1;
			printf("* Write tracks backwards\n");
			break;

		case 'P':
			printf("* Skip 1571 SRQ Support (Use parallel always)\n");
			override_srq = 1;
			break;

		case 'h':
			if(track_inc == 1) track_inc = 2;
			else track_inc = 1;
			printf("* Toggle halftracks (increment=%d)\n",track_inc);
			break;

		case 'I':
			increase_sync = atoi(&(*argv)[2]);
			if(!increase_sync) increase_sync=1;
			printf("* Fix/increase short syncs by %d\n", increase_sync);
			break;

		case 'S':
			if (!(*argv)[2]) usage();
			st = atof(&(*argv)[2])*2;
			start_track = (int)st;
			printf("* Start track set to %.1f (%d)\n", st/2, start_track);
			break;

		case 'E':
			if (!(*argv)[2]) usage();
			et = atof(&(*argv)[2])*2;
			end_track = (int)et;
			if((et/2)>41) printf("WARNING: Most drives won't reach past 41 tracks and your head carriage can physically JAM!\n");
			if((et/2)>MAX_TRACKS_1541)
			{
				printf("WARNING: MAX tracks is %d\n",MAX_TRACKS_1541);
				end_track=(MAX_TRACKS_1541*2);
			}
			printf("* End track set to %.1f (%d)\n", et/2, end_track);
			break;

		case 'z':
			nb2cycle = atoi(&(*argv)[2]);
			printf("* NB2 cycle chosen = %d\n", nb2cycle);
			break;

		case 'u':
		case 'w':
			mode = MODE_UNFORMAT_DISK;
			unformat_passes = atoi(&(*argv)[2]);
			if(!unformat_passes) unformat_passes = 1;
			printf("* Unformat passes = %d\n", unformat_passes);
			track_inc = 1;
			break;

		case 'R':
			// hidden secret raw track file writing mode
			printf("* Raw track dump write mode\n");
			mode = MODE_WRITE_RAW;
			break;

		case 'A':
			printf("* Speed adjustment mode\n");
			mode = MODE_SPEED_ADJUST;
			break;

		case 'p':
			// custom protection handling
			printf("* Custom copy protection handler: ");
			switch((*argv)[2])
			{
				case 'x':
					printf("V-MAX!\n");
					memset(align_map, ALIGN_VMAX, MAX_TRACKS_1541+1);
					fix_gcr = 0;
					presync = 1;
					break;

				case 'c':
					printf("V-MAX! (CINEMAWARE)\n");
					memset(align_map, ALIGN_VMAX_CW, MAX_TRACKS_1541+1);
					fix_gcr = 0;
					presync = 1;
					break;

				case 'g':
				case 'm':
					printf("SecuriSpeed/Early Rainbow Arts\n"); /* turn off reduction for track > 36 */
					for(count = 36; count <= MAX_TRACKS_1541+1; count ++)
					{
						reduce_map[count] = REDUCE_NONE;
						align_map[count] = ALIGN_AUTOGAP;
					}
					fix_gcr = 0;
					break;

				case 'v':
					printf("VORPAL (NEWER)\n");
					memset(align_map, ALIGN_AUTOGAP, MAX_TRACKS_1541+1);
					align_map[18] = ALIGN_NONE;
					fillbyte=0x55;
					break;

				case'r':
					printf("RAPIDLOK\n"); /* don't reduce sync, but everything else */
					//for(count = 1; count <= MAX_TRACKS_1541; count ++)
					//	reduce_map[count] = REDUCE_BAD | REDUCE_GAP;
					memset(align_map, ALIGN_RAPIDLOK, MAX_TRACKS_1541+1);
					break;

				case'p':
					printf("Pirateslayer\n");
					align_map[2] = ALIGN_PSLAYER;
					align_map[36] = ALIGN_PSLAYER;
					align_map[37] = ALIGN_PSLAYER;
					break;

				default:
					printf("Unknown protection handler\n");
					break;
			}
			break;

		case 'a':
			// custom alignment handling
			printf("* Custom alignment: ");
			if ((*argv)[2] == '0')
			{
				printf("sector 0\n");
				memset(align_map, ALIGN_SEC0, MAX_TRACKS_1541+1);
			}
			else if ((*argv)[2] == 'g')
			{
				printf("gap\n");
				memset(align_map, ALIGN_GAP, MAX_TRACKS_1541+1);
			}
			else if ((*argv)[2] == 'w')
			{
				printf("longest bad GCR run\n");
				memset(align_map, ALIGN_BADGCR, MAX_TRACKS_1541+1);
			}
			else if ((*argv)[2] == 's')
			{
				printf("longest sync\n");
				memset(align_map, ALIGN_LONGSYNC, MAX_TRACKS_1541+1);
			}
			else if ((*argv)[2] == 'a')
			{
				printf("autogap\n");
				memset(align_map, ALIGN_AUTOGAP, MAX_TRACKS_1541+1);
			}
			else if ((*argv)[2] == 'n')
			{
				printf("raw (no alignment, use NIB start)\n");
				memset(align_map, ALIGN_RAW, MAX_TRACKS_1541+1);
			}
			else
				printf("Unknown alignment parameter\n");
			break;

		case 'r':
			reduce_sync = atoi(&(*argv)[2]);
			if(reduce_sync)
			{
				printf("* Reduce sync to %d bytes\n", reduce_sync);
			}
			else
			{
				printf("* Disabled sync reduction\n");
				for(count = 1; count <= MAX_TRACKS_1541; count ++)
				reduce_map[count] &= ~REDUCE_SYNC;
			}
			break;

		case '0':
			printf("* Reduce bad GCR enabled\n");
			for(count = 1; count <= MAX_TRACKS_1541; count ++)
				reduce_map[count] |= REDUCE_BAD;
			break;

		case 'g':
			printf("* Reduce gaps enabled\n");
			for(count = 1; count <= MAX_TRACKS_1541; count ++)
				reduce_map[count] |= REDUCE_GAP;
			break;

		case 'D':
			if (!(*argv)[2]) usage();
			drive = (BYTE) atoi(&(*argv)[2]);
			printf("* Use Device %d\n", drive);
			break;

		case 'G':
			if (!(*argv)[2]) usage();
			gap_match_length = atoi(&(*argv)[2]);
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'f':
			if (!(*argv)[2])
				fix_gcr = 0;
			else
				fix_gcr = atoi(&(*argv)[2]);
			printf("* Enabled level %d 'bad' GCR reproduction.\n", fix_gcr);
			break;

		case 'v':
			verbose++;
			printf("* Verbosity on level %d\n", verbose);
			break;

		case 'V':
			track_match = 1;
			printf("* Enable track match verify\n");
			break;

		case 'c':
			auto_capacity_adjust = 0;
			printf("* Disabled automatic capacity adjustment\n");
			break;

		case 'm':
			if (!(*argv)[2])
				extra_capacity_margin = 0;
			else
				extra_capacity_margin = atoi(&(*argv)[2]);
			printf("* Changed extra capacity margin to %d\n", extra_capacity_margin);
			break;

		case 'M':
			printf("* Minimum capacity ignore on\n");
			cap_min_ignore = 1;
			break;

		case 'o':
			printf("* Use old hard-coded G64 format\n");
			old_g64 = 1;
			break;

		case 'T':
			if (!(*argv)[2]) usage();
			skew = (atoi(&(*argv)[2]));
			if((skew > 200) || (skew < 0))
			{
				printf("Skew must be between 1 and 200ms\n");
				skew = 0;
			}
			printf("* Skew set to %dms\n",skew);

		case 't':
			if(!ihs) align_disk = 1;
			printf("* Attempt timer-based track alignment\n");
			break;

		case 'i':
			printf("* 1571 or SuperCard-compatible index hole sensor (use only for side 1)\n");
			align_disk = 0;
			ihs = 1;
			break;

		case 'C':
			rpm_real = atoi(&(*argv)[2]);
			printf("* Simulate track capacity: %dRPM\n",rpm_real);
			if(rpm_real < 282)
			{
					printf("ERROR: RPM too slow, cannot store track larger than 8192 bytes!");
					exit(1);
			}
			break;

		case 'F':
			/* override FAT track detection */
			fattrack = atoi(&(*argv)[2]);
			if(!fattrack)
			{
				printf("* FAT tracks disabled\n");
				fattrack=99;
			}
			else
			{
				printf("* Insert FAT track on %d/%.2f/%d\n",fattrack,fattrack+0.5,fattrack+1);
				fattrack*=2;
			}
			break;

		case 'x':
			presync = atoi(&(*argv)[2]);
			if(!presync) presync=1;
			printf("* Add short sync bytes to start of each track:%d\n",presync);
			break;

		case 'b':
			// custom fillbyte
			printf("* Custom fillbyte: ");
			//if ((*argv)[2] == '0')
			//{
			//	printf("$00\n");
			//	fillbyte = 0x00;
			//}
			//if ((*argv)[2] == '5')
			//{
			//	printf("$55\n");
			//	fillbyte = 0x55;
			//}
			//if ((*argv)[2] == 'f')
			//{
			//	printf("$ff\n");
			//	fillbyte = 0xff;
			//}
			if ((*argv)[2] == 'a')
			{
				/* adaptive */
				fillbyte = 0xfe;
			}
			else
			{
				fillbyte = (unsigned char)strtol(&(*argv)[2], NULL, 16);
			}
			printf("%x\n",fillbyte);
			break;

		/* this is only used in reading or unformat */
		case 'k':
			read_killer = 0;
			printf("* Ignore 'killer' tracks\n");
			break;

		default:
			usage();
			break;
	}
}

void switchusage(void)
{
	printf(
	" -a[x]: Force alternative track alignments (advanced users only)\n"
	" -p[x]: Custom protection handlers (advanced users only)\n"
 	" -f[n]: Enable level 'n' aggressive bad GCR simulation\n"
	" -G[n]: Alternate gap match length\n"
	" -C[n]: Simulate 'n' RPM track capacity\n"
	" -T[n]: Track skew simulation (in ms, max 200ms)\n"
 	" -g: Enable gap reduction\n"
 	" -0: Enable bad GCR run reduction\n"
 	" -r: Disable automatic sync reduction\n"
	" -f: Disable automatic bad GCR simulation\n"
	//" -b: Custom fillbyte (0,5,f,a=adaptive)\n"
	" -v: Verbose (output more detailed info)\n");
}

int load_file(char *filename, BYTE *file_buffer)
{
	int size;
	FILE *fpin;

	printf("Loading \"%s\"...\n",filename);

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		printf("Couldn't open input file %s!\n", filename);
		return 0;
	}

	fseek(fpin, 0, SEEK_END);
	size = ftell(fpin);
	rewind(fpin);

	if (fread(file_buffer, size, 1, fpin) != 1) {
			printf("unable to read file\n");
			return 0;
	}

	printf("Successfully loaded %d bytes\n", size);
	fclose(fpin);
	return size;
}

int read_nib(BYTE *file_buffer, int file_buffer_size, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track, t_index=0, h_index=0;

	printf("Parsing NIB data...\n");

	if (memcmp(file_buffer, "MNIB-1541-RAW", 13) != 0)
	{
		printf("Not valid NIB data!\n");
		return 0;
	}
	else
		printf("NIB file version %d\n", file_buffer[13]);

	while(file_buffer[0x10+h_index])
	{
		track = file_buffer[0x10+h_index];
		track_density[track] = (BYTE)(file_buffer[0x10 + h_index + 1]);
		track_density[track] %= BM_MATCH;  	 /* discard unused BM_MATCH mark */

		memcpy(track_buffer + (track * NIB_TRACK_LENGTH),
			file_buffer + (t_index * NIB_TRACK_LENGTH) + 0x100,
			NIB_TRACK_LENGTH);

		h_index+=2;
		t_index++;
	}
	printf("Successfully parsed NIB data for %d tracks\n", t_index);
	return 1;
}

int read_nb2(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length, size_t cycle)
{
	int track, pass_density, nibsize, temp_track_inc, numtracks;
	int header_entry = 0;
	size_t pass;
	char header[0x100];
	BYTE nibdata[0x2000];
	BYTE tmpdata[0x2000];
	BYTE diskid[2], dummy;
	FILE *fpin;
	size_t errors, best_err, best_pass;
	size_t length, best_len;
	char errorstring[0x1000];
	char testfilename[16];
	FILE *trkout;

	printf("Reading NB2 file...\n");

	temp_track_inc = 1;  /* all nb2 files contain halftracks */

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		printf("Couldn't open input file %s!\n", filename);
		return 0;
	}

	if (fread(header, sizeof(header), 1, fpin) != 1)
	{
		printf("unable to read NIB header\n");
		return 0;
	}

	if (memcmp(header, "MNIB-1541-RAW", 13) != 0)
	{
		printf("input file %s isn't an NB2 data file!\n", filename);
		return 0;
	}

	/* Determine number of tracks in image (estimated by filesize) */
	fseek(fpin, 0, SEEK_END);
	nibsize = ftell(fpin);
	numtracks = (nibsize - NIB_HEADER_SIZE) / (NIB_TRACK_LENGTH * 16);
	temp_track_inc = 1;
	printf("\n%d track image (filesize = %d bytes)\n", numtracks, nibsize);

	/* get disk id */
	rewind(fpin);
	fseek(fpin, sizeof(header) + (17 * 2 * NIB_TRACK_LENGTH * 16) + (8 * NIB_TRACK_LENGTH), SEEK_SET);
	fread(tmpdata, NIB_TRACK_LENGTH, 1, fpin);

	if (!extract_id(tmpdata, diskid))
	{
			printf("Cannot find directory sector.\n");
			return 0;
	}
	if(verbose) printf("\ndiskid: %c%c\n", diskid[0], diskid[1]);

	rewind(fpin);
	if (fread(header, sizeof(header), 1, fpin) != 1) {
		printf("unable to read NB2 header\n");
		return 0;
	}

	for (track = 2; track <= end_track; track += temp_track_inc)
	{
		/* get density from header or use default */
		track_density[track] = (BYTE)(header[0x10 + (header_entry * 2) + 1]);
		header_entry++;

		best_pass = 0;
		best_err = 0;
		best_len = 0;  /* unused for now */

		if(verbose) printf("\n%4.1f:",(float) track / 2);

		/* contains 16 passes of track, four for each density */
		for(pass_density = 0; pass_density < 4; pass_density ++)
		{
			if(verbose) printf(" (%d)", pass_density);

			for(pass = 0; pass <= 3; pass ++)
			{
				/* get track from file */
				fread(nibdata, NIB_TRACK_LENGTH, 1, fpin);
				if(pass>(cycle-1)) continue;

				length = extract_GCR_track(tmpdata, nibdata,
					&dummy,
					track/2,
					capacity_min[track_density[track]&3],
					capacity_max[track_density[track]&3]);

				errors = check_errors(tmpdata, length, track, diskid, errorstring);

				//printf("D:%d,L:%d,E:%d",track_density[track]&3,length,errors);

				if(pass_density == (track_density[track]&3))
				{
					if( (pass==(cycle-1)) || (errors < best_err) )
					{
						//track_length[track] = 0x2000;
						memcpy(track_buffer + (track * NIB_TRACK_LENGTH), nibdata, NIB_TRACK_LENGTH);
						best_pass = pass;
						best_err = errors;
					}
				}
				sprintf(testfilename, "raw/tr%.1fd%d", (float) track/2, pass_density);
				if(NULL != (trkout = fopen(testfilename, "w")))
				{
					fwrite(nibdata, NIB_TRACK_LENGTH, 1, trkout);
					fclose(trkout);
				}
			}
		}

		/* output some specs */
		if(verbose)
		{
			printf(" (");
			if(track_density[track] & BM_NO_SYNC) printf("NOSYNC!");
			if(track_density[track] & BM_FF_TRACK) printf("KILLER!");

			printf("%d:%d) (pass %d, %d errors) %.1d%%", track_density[track]&3, track_length[track],
				best_pass+1, best_err,
				((track_length[track] / capacity[track_density[track]&3]) * 100));
		}
	}
	fclose(fpin);
	//printf("Successfully loaded NB2 file\n");
	return 1;
}

int read_g64(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track, g64maxtrack, g64tracks, headersize;
	int pointer=0;
	BYTE header[0x7f0];
	BYTE length_record[2];
	FILE *fpin;

	printf("Reading G64 file...\n");

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		printf("Couldn't open input file %s!\n", filename);
		return 0;
	}

	if (fread(header, sizeof(header), 1, fpin) != 1)
	{
		printf("unable to read G64 header\n");
		return 0;
	}

	if (memcmp(header, "GCR-1541", 8) != 0)
	{
		printf("input file %s isn't a G64 data file!\n", filename);
		return 0;
	}

	if (memcmp(header+0x2ac, "EXT", 3) == 0)
	{
		printf("\nExtended SPS G64 detected\n");
		headersize=0x7f0;
		if(!sync_align_buffer) sync_align_buffer=1;
		else
		{
			sync_align_buffer=0;
			printf("SPS file, but sync align was disabled by switch\n");
		}
	}
	else
		headersize=0x2ac;

	g64tracks = (char)header[0x9];
	g64maxtrack = (BYTE)header[0xb] << 8 | (BYTE)header[0xa];
	if(verbose) printf("\nTracks:%d\nSize:%d\n", g64tracks, g64maxtrack);
	rewind(fpin);

	if (fread(header, headersize, 1, fpin) != 1)
	{
		printf("unable to read G64 header\n");
		return 0;
	}

	if(g64maxtrack>NIB_TRACK_LENGTH)
	{
			printf("\nContains too large track!\nLikely corrupt G64 file\nWill attempt to skip bad tracks\n");
			//return 0;
	}

	for (track = 2; track <= g64tracks; track++, pointer += 4)
	{
		int pointer2 = *(int*)(header+0xc+pointer);
		int tmpLength;

		/* check to see if track exists in file, else skip it */
		if(!pointer2)
		{
			track_length[track]=0;
			continue;
		}

		/* get density from header */
		track_density[track] = header[0x15c + pointer];

		fseek(fpin, pointer2, SEEK_SET);
		//printf("\nDEBUG: filepointer=%d\n",pointer2);

		/* get length */
		fread(length_record, 2, 1, fpin);
		tmpLength = length_record[1] << 8 | length_record[0];

		if(tmpLength>NIB_TRACK_LENGTH)
		{
			tmpLength = NIB_TRACK_LENGTH;
			//printf(" skipping extra data");
		}
		track_length[track] = tmpLength;

		/* get track from file */
		fread(track_buffer + (track * NIB_TRACK_LENGTH), tmpLength, 1, fpin);

		/* output some specs */
		if(verbose)
		{
			printf("%4.1f: ",(float) track/2);
			if(track_density[track] & BM_NO_SYNC) printf("NOSYNC!");
			if(track_density[track] & BM_FF_TRACK) printf("KILLER!");
			printf("%d (density:%d)\n", track_length[track], track_density[track]);
		}
	}
	fclose(fpin);
	//printf("Successfully loaded G64 file\n");
	return 1;
}


int read_d64(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track, sector, sector_ref;
	BYTE buffer[256];
	BYTE gcrdata[NIB_TRACK_LENGTH];
	BYTE errorinfo[MAXBLOCKSONDISK];
	BYTE id[3] = { 0, 0, 0 };
	int error, d64size, last_track, cur_sector=0;
	char errorstring[0x1000], tmpstr[8];
	FILE *fpin;

	printf("Reading D64 file...\n");

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		printf("Couldn't open input file %s!\n", filename);
		return 0;
	}

	/* here we get to rebuild tracks from scratch */
	memset(errorinfo, SECTOR_OK, sizeof(errorinfo));

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

	default:  // non-standard images, attempt to load anyway
		//rewind(fpin);
		//printf("Bad d64 image size.\n");
		//return 0;
		printf("\nNon-standard D64 image... attempting to load as 40-track anyway\n");
		printf("%d sectors in file\n", d64size/256);
		last_track = 40;
		break;
	}

	// determine disk id from track 18 (offsets $165A2, $165A3)
	fseek(fpin, 0x165a2, SEEK_SET);
	fread(id, 2, 1, fpin); // @@@SRT: check success
	rewind(fpin);

	sector_ref = 0;
	for (track = 1; track <= last_track; track++)
	{
		// clear buffers
		memset(gcrdata, 0x55, sizeof(gcrdata));
		errorstring[0] = '\0';

		for (sector = 0; sector < sector_map[track]; sector++)
		{
			// get error and increase reference pointer in errorinfo
			error = errorinfo[sector_ref++];

			if (error != SECTOR_OK)
			{
				sprintf(tmpstr, " E%dS%d", error, sector);
				strcat(errorstring, tmpstr);
			}

			// read sector from file
			if(d64size/256 > cur_sector)
				fread(buffer, 256, 1, fpin); // @@@SRT: check success
			else
				memset(buffer, fillbyte, sizeof(buffer));

			// convert to gcr
			convert_sector_to_GCR(buffer, gcrdata + (sector * (SECTOR_SIZE + sector_gap_length[track])), track, sector, id, error);
			cur_sector++;
		}

		// no half tracks in D64, so clear them
		track_length[(track*2)+1] = 0;

		// use default densities for D64
		track_density[track*2] = speed_map[track];

		// calculate track length
		//track_length[track*2] = sector_map[track] * (SECTOR_SIZE + sector_gap_length[track]);

		// handle track "stretch"
		if(rpm_real)
		{
			switch (track_density[track*2])
			{
				case 0:
					capacity[speed_map[track]] = (size_t)(DENSITY0/rpm_real);
					break;
				case 1:
					capacity[speed_map[track]] = (size_t)(DENSITY1/rpm_real);
					break;
				case 2:
					capacity[speed_map[track]] = (size_t)(DENSITY2/rpm_real);
					break;
				case 3:
					capacity[speed_map[track]] = (size_t)(DENSITY3/rpm_real);
					break;
			}
			//printf("[%d] = %d\n",track_density[track*2],capacity[speed_map[track*2]]);
		}

		// use real/calculated track length
		track_length[track*2] = capacity[speed_map[track]&3];

		// render track
		memcpy(track_buffer + (track * 2 * NIB_TRACK_LENGTH), gcrdata, track_length[track*2]);
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
	//printf("Successfully loaded D64 file\n");
	return 1;
}

int save_file(char *filename, BYTE *file_buffer, int length)
{
		FILE *fpout;

		/* create output file */
		if ((fpout = fopen(filename, "wb")) == NULL)
		{
			printf("Couldn't create output file %s!\n", filename);
			return 0;
		}

		if(!(fwrite(file_buffer, length, 1, fpout)))
		{
			printf("Couldn't write to output file %s!\n", filename);
			return 0;
		}

		fclose(fpout);
		printf("Successfully saved file %s\n", filename);
		return 1;
}

int write_nib(BYTE*file_buffer, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
    /*	writes contents of buffers into NIB file, with header and density information
			it does not process the track
    */

	int track;
	char header[0x100];
	int header_entry = 0;

	printf("Converting to NIB format...\n");

	/* clear header */
	memset(header, 0, sizeof(header));

	/* header now contains whether halftracks were read */
	if(track_inc == 1)
		sprintf(header, "MNIB-1541-RAW%c%c%c", 3, 0, 1);
	else
		sprintf(header, "MNIB-1541-RAW%c%c%c", 3, 0, 0);

	for (track = start_track; track <= end_track; track += track_inc)
	{
		header[0x10 + (header_entry * 2)] = (BYTE)track;
		header[0x10 + (header_entry * 2) + 1] = track_density[track];

		/* process and save track to disk */
		memcpy(file_buffer + sizeof(header) + (NIB_TRACK_LENGTH * header_entry),
			track_buffer + (NIB_TRACK_LENGTH * track), NIB_TRACK_LENGTH);

		header_entry++;
	}
	memcpy(file_buffer, header, sizeof(header));
	//printf("Successfully parsed data to NIB format\n");

	return (sizeof(header) + (header_entry * NIB_TRACK_LENGTH));
}


int write_d64(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
    /*	writes contents of buffers into D64 file, with errorblock information (if detected) */

	FILE *fpout;
	int track, sector;
	int errors = 0;
	int hi_errors = 0;
	int save_errorinfo = 0;
	int save_40_errors = 0;
	int save_40_tracks = 0;
	int blockindex = 0;
	int offset = 0;
	BYTE *cycle_start;	/* start position of cycle    */
	BYTE *cycle_stop;	/* stop  position of cycle +1 */
	BYTE id[4];
	BYTE rawdata[260];
	BYTE d64data[MAXBLOCKSONDISK * 256], *d64ptr;
	BYTE errorinfo[MAXBLOCKSONDISK], errorcode;
	int blocks_to_save;

	printf("Writing D64 file...\n");

	if (!extract_id(track_buffer + (18 * 2 * NIB_TRACK_LENGTH), id))
	{
		printf("Cannot find directory sector.\nAborted!");
		return 0;
	}

	memset(errorinfo, 0,sizeof(errorinfo));
	memset(rawdata, 0,sizeof(rawdata));
	memset(d64data, 0,sizeof(d64data));

	/* create output file */
	if ((fpout = fopen(filename, "wb")) == NULL)
	{
		printf("Couldn't create output file %s!\n", filename);
		return 0;
	}

	/* get disk id */
	if (!extract_id(track_buffer + (18*2*NIB_TRACK_LENGTH), id))
	{
		int track = id[0];
		//printf("debug: dir track really=%d\n",track);
		offset = 18 - track;
		if (!offset || !extract_id(track_buffer + ((18+offset)*2*NIB_TRACK_LENGTH), id))
		{
			printf("Cannot find directory sector.\n");
			return 0;
		}
		else
		{
			printf("Track offset found in image: %d\n",offset);
			//offset++; // the rest of the routines for D64 only operate on every other track
		}
	}
	//printf("debug: diskid=%s\n",id);

	d64ptr = d64data;
	for (track = start_track; track <= 40*2; track += 2)
	{
		cycle_start = track_buffer + ((track+(offset*2)) * NIB_TRACK_LENGTH);
		cycle_stop = track_buffer + ((track+(offset*2)) * NIB_TRACK_LENGTH) + track_length[track+(offset*2)];
		//printf("debug: start=%d, stop=%d\n",cycle_start,cycle_stop);

		if(verbose) printf("%.2d (%d):" ,track/2, capacity[speed_map[track/2]]);

		if (track < 2 || track > 80)
		{
		  for (sector = 0; sector < sector_map[track/2]; sector++)
			errorinfo[blockindex] = SYNC_NOT_FOUND;
		}
		else
		for (sector = 0; sector < sector_map[track/2]; sector++)
		{
			if(verbose) printf("%d", sector);

			memset(rawdata, 0,sizeof(rawdata));
			errorcode = convert_GCR_sector(cycle_start, cycle_stop, rawdata, track/2, sector, id);
			errorinfo[blockindex] = errorcode;	/* OK by default */

			if (errorcode != SECTOR_OK)
			{
				if (track/2 <= 35)
				{
					save_errorinfo = 1;
					errors++;
				}
				else
				{
					save_40_errors = 1;
					hi_errors++;
				}
			}
			if((track/2 > 35) &&
				(errorcode != SYNC_NOT_FOUND) &&
				(errorcode != HEADER_NOT_FOUND))
			{
				save_40_tracks = 1;
			}

			/* screen information */
			if (errorcode == SECTOR_OK)
			{
				if(verbose) printf(" ");
			}
			else
			{
				if(verbose)
					printf("%.1x", errorcode);
				else
					if(track/2<=35)
						printf("Error %.1d on Track %d, Sector %d\n", errorcode, track/2, sector);
			}

			/* dump to buffer */
			memcpy(d64ptr, rawdata+1 , 256);
			d64ptr += 256;

			blockindex++;
		}
		if(verbose) printf("\n");
	}
	if(verbose) printf("\n");

	blocks_to_save = (save_40_tracks) ? MAXBLOCKSONDISK : BLOCKSONDISK;

	if (fwrite(d64data, blocks_to_save * 256, 1, fpout) != 1)
	{
		printf("Cannot write d64 data.\n");
		return 0;
	}

	if (save_errorinfo == 1)
	{
		assert(sizeof(errorinfo) >= (size_t)blocks_to_save);

		if (fwrite(errorinfo, blocks_to_save, 1, fpout) != 1)
		{
			printf("Cannot write sector data.\n");
			return 0;
		}

		if(blocks_to_save > 683)
			printf("Converted %d errors into errorblock\n", errors+hi_errors);
		else
			printf("Converted %d errors into errorblock\n", errors);
	}

	fclose(fpout);
	printf("Converted %d blocks into D64 file\n", blocks_to_save);
	return 1;
}


int write_g64(char *filename, BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	/* writes contents of buffers into G64 file, with header and density information */

	/* when writing a G64 file, we don't care about the limitations of drive hardware
		However, VICE (previous to version 2.2) ignored the G64 header and hardcodes 7928 as the largest
		track size, and also required it to be 84 tracks no matter if they're used or not.
	*/

	DWORD g64_max_tracklen = 0;
	BYTE header[12];
	DWORD gcr_track_p[MAX_HALFTRACKS_1541] = {0};
	DWORD gcr_speed_p[MAX_HALFTRACKS_1541] = {0};
	//BYTE gcr_track[g64_max_tracklen + 2];
	BYTE gcr_track[NIB_TRACK_LENGTH + 2];
	size_t track_len, badgcr;
	//size_t skewbytes=0;
	int index=0, track, added_sync=0, addsyncloops;
	FILE * fpout;
	BYTE buffer[NIB_TRACK_LENGTH];
	size_t raw_track_size[4] = { 6250, 6666, 7142, 7692 };
	//char errorstring[0x1000];

	printf("Writing G64 file...\n");
	printf("RPM set to %d\n",rpm_real);

	fpout = fopen(filename, "wb");
	if (fpout == NULL)
	{
		printf("Cannot open G64 image %s.\n", filename);
		return 0;
	}

	/* determine max track size (old VICE can't handle) */
	if(!old_g64)
	{
		for (track= 0; track < MAX_HALFTRACKS_1541; track += track_inc)
		{
			if((track_length[track+2] != 8192) && (track_length[track+2] > g64_max_tracklen))
			{
				g64_max_tracklen = track_length[track+2];
				if(verbose) printf("Longer Track %4.1f = %d\n",(float)(track+2)/2,g64_max_tracklen);
			}
		}
	}
	else
		g64_max_tracklen = 7928; // old hardcoded value
	printf("G64 Track Length = %d", g64_max_tracklen);

	/* Create G64 header */
	strcpy((char *) header, "GCR-1541");
	header[8] = 0;	/* G64 version */
	header[9] = MAX_HALFTRACKS_1541; /* Number of Halftracks  (VICE <2.2 can't handle non-84 track images) */
	//header[9] = (unsigned char)end_track;
	header[10] = (BYTE) (g64_max_tracklen % 256);	/* Size of each stored track */
	header[11] = (BYTE) (g64_max_tracklen / 256);

	if (fwrite(header, sizeof(header), 1, fpout) != 1)
	{
		printf("Cannot write G64 header.\n");
		return 0;
	}

	/* Create track and speed tables */
	for (track = 0; track < MAX_HALFTRACKS_1541; track +=track_inc)
	{
		/* calculate track positions and speed zone data */
		if(!track_length[track+2]) continue;

		gcr_track_p[track] = 0xc + (MAX_TRACKS_1541 * 16) + (index++ * (g64_max_tracklen + 2));
		gcr_speed_p[track] = track_density[track+2]&3;
	}

	/* write headers */
	if (write_dword(fpout, gcr_track_p, sizeof(gcr_track_p)) < 0)
	{
		printf("Cannot write track header.\n");
		return 0;
	}

	if (write_dword(fpout, gcr_speed_p, sizeof(gcr_speed_p)) < 0)
	{
		printf("Cannot write speed header.\n");
		return 0;
	}

	/* shuffle raw GCR between formats */
	for (track = 2; track <= MAX_HALFTRACKS_1541+1; track +=track_inc)
	{
		fillbyte = track_buffer[(track * NIB_TRACK_LENGTH) + track_length[track] - 1];
		memset(buffer, fillbyte, sizeof(buffer));

		track_len = track_length[track];
		//if(track_len>g64_max_tracklen) track_len=g64_max_tracklen;

		if(!track_len) continue;

		memcpy(buffer, track_buffer + (track * NIB_TRACK_LENGTH), track_len);

		/* user display */
		if(verbose)
		{
			printf("\n%4.1f: (", (float)track/2);
			printf("%d", track_density[track]&3);
			if ( (track_density[track]&3) != speed_map[track/2]) printf("!");
			printf(":%d) ", track_length[track]);
			if (track_density[track] & BM_NO_SYNC) printf("NOSYNC ");
			if (track_density[track] & BM_FF_TRACK) printf("KILLER ");
		}

		/* process/compress GCR data */
		if(increase_sync)
		{
			for(addsyncloops=0;addsyncloops<increase_sync;addsyncloops++)
			{
				added_sync = lengthen_sync(buffer, track_len, g64_max_tracklen);
				track_len += added_sync;
				if(verbose) printf("[+sync:%d]", added_sync);
			}
		}

		badgcr = check_bad_gcr(buffer, track_len);
		if(verbose>1) printf("(weak:%d)",badgcr);

		if(rpm_real)
		{
			//capacity[speed_map[track/2]] = raw_track_size[speed_map[track/2]];
			switch (track_density[track])
			{
				case 0:
					capacity[speed_map[track/2]] = (size_t)(DENSITY0/rpm_real);
					break;
				case 1:
					capacity[speed_map[track/2]] = (size_t)(DENSITY1/rpm_real);
					break;
				case 2:
					capacity[speed_map[track/2]] = (size_t)(DENSITY2/rpm_real);
					break;
				case 3:
					capacity[speed_map[track/2]] = (size_t)(DENSITY3/rpm_real);
				break;
			}

			//printf("\ntrack=%d density=%d rpmreal=%d speedmap=%d capacity:%d\n",track,DENSITY0,rpm_real,speed_map[track/2],capacity[speed_map[track/2]]);

			if(capacity[speed_map[track/2]] > g64_max_tracklen)
				capacity[speed_map[track/2]] = g64_max_tracklen;

			if(track_len > capacity[speed_map[track/2]])
			{
				printf("\nTrack %d too long (%d) for %d RPM and will be processed!",track/2,track_len,rpm_real);
				track_len = compress_halftrack(track, buffer, track_density[track], track_len);
				printf(" (%d)", track_len);
			}
			if(verbose) printf(" (%d)", track_len);
		}
		else
		{
			capacity[speed_map[track/2]] = g64_max_tracklen;
			if(track_len > capacity[speed_map[track/2]])
			{
				printf("\nTrack %d too long for %d RPM and will be processed!",track/2,rpm_real);
				verbose+=1;
			}
				track_len = compress_halftrack(track, buffer, track_density[track], track_len);
		}
		if(verbose>1) printf("(fill:$%.2x)",fillbyte);

		gcr_track[0] = (BYTE) (track_len % 256);
		gcr_track[1] = (BYTE) (track_len / 256);

		/* apply skew, if specified */
		//if(skew)
		//{
		//	skewbytes = skew * (capacity[track_density[track]&3] / 200);
		//	if(skewbytes > track_len)
		//		skewbytes = skewbytes - track_len;
		//printf(" {skew=%d} ", skewbytes);
		//}
		//memcpy(gcr_track+2, buffer+skewbytes, track_len-skewbytes);
		//memcpy(gcr_track+2+track_len-skewbytes, buffer, skewbytes);

		memcpy(gcr_track+2, buffer, track_len);

		if (fwrite(gcr_track, (g64_max_tracklen + 2), 1, fpout) != 1)
		{
			printf("Cannot write track data.\n");
			return 0;
		}
	}
	fclose(fpout);
	//printf("\nSuccessfully saved G64 file\n");
	return 1;
}

size_t compress_halftrack(int halftrack, BYTE *track_buffer, BYTE density, size_t length)
{
	size_t orglen;
	BYTE gcrdata[NIB_TRACK_LENGTH];

	/* copy to spare buffer */
	memcpy(gcrdata, track_buffer, NIB_TRACK_LENGTH);
	memset(track_buffer, 0, NIB_TRACK_LENGTH);

	/* process and compress track data (if needed) */
	if (length > 0)
	{
		/* If our track contains sync, we reduce to a minimum of 32 bits
		   less is too short for some loaders including CBM, but only 10 bits are technically required */
		orglen = length;
		if ( (length > (capacity[density&3])) && (!(density & BM_NO_SYNC)) &&
			(reduce_map[halftrack/2] & REDUCE_SYNC) )
		{
			/* reduce sync marks within the track */
			length = reduce_runs(gcrdata, length, capacity[density&3], reduce_sync, 0xff);
			if(verbose) printf("[sync:-%d]", orglen - length);
		}

		/* reduce bad GCR runs */
		orglen = length;
		if ( (length > (capacity[density&3])) &&
			(reduce_map[halftrack/2] & REDUCE_BAD) )
		{
			length = reduce_runs(gcrdata, length, capacity[density&3], 0, 0x00);
			if(verbose) printf("[badgcr:-%d]", orglen - length);
		}

		/* reduce sector gaps -  they occur at the end of every sector and vary from 4-19 bytes, typically  */
		orglen = length;
		if ( (length > (capacity[density&3])) &&
			(reduce_map[halftrack/2] & REDUCE_GAP) )
		{
			length = reduce_gaps(gcrdata, length, capacity[density & 3]);
			if(verbose) printf("[gaps:-%d]", orglen - length);
		}

		/* still not small enough, we have to truncate the end (reduce tail) */
		orglen = length;
		if (length > capacity[density&3])
		{
			length = capacity[density&3];
			if(verbose) printf("[trunc:-%d]", orglen - length);
		}
	}

	/* if track is empty (unformatted) fill with '0' bytes to simulate */
	if ( (!length) && (density & BM_NO_SYNC))
	{
		memset(gcrdata, 0, NIB_TRACK_LENGTH);
		length = NIB_TRACK_LENGTH;
	}

	/* write processed track buffer */
	memcpy(track_buffer, gcrdata, length);
	return length;
}

int sync_tracks(BYTE *track_buffer, BYTE *track_density, size_t *track_length, BYTE *track_alignment)
{
	int track;
	BYTE temp_buffer[NIB_TRACK_LENGTH*2];
	//BYTE *nibdata_aligned; // aligned track
	//int aligned_len;       // aligned track length

	printf("Sync-aligning tracks...\n");
	for (track = start_track; track <= end_track; track ++)
	{
		if(track_length[track])
		{
			if(verbose) printf("\n%4.1f: (%d) ",(float) track/2, track_length[track]);

			if(track_length[track]==NIB_TRACK_LENGTH) continue;

			check_bad_gcr(track_buffer+(track*NIB_TRACK_LENGTH), track_length[track]);

			/* Pete's version */
			if(!sync_align(track_buffer+(track*NIB_TRACK_LENGTH), track_length[track]))
			{
					printf("{nosync}");
					continue;
			}
			/* end Pete's version */

			/* Arnd's version */
			//if (isTrackBitshifted(track_buffer+(track*NIB_TRACK_LENGTH), track_length[track]))
			//{
			//	printf("[bitshifted] ");
			//	align_bitshifted_kf_track(track_buffer+(track*NIB_TRACK_LENGTH), track_length[track], &nibdata_aligned, &aligned_len);

			//	if(aligned_len<0x2000)
			//		track_length[track] = aligned_len;
			//	else
			//	{
			//		aligned_len = 0x2000;
			//		printf("aligned data too long, truncated ");
			//	}
			//	memcpy(track_buffer+(track*NIB_TRACK_LENGTH), nibdata_aligned, aligned_len);
			//}
			//else continue; // Continue if track is aligned or if no sync found.
			/* end Arnd version */

			/* re-extract/align data, since KF images are just index to index */
			if(sync_align_buffer < 2)
			{
				memcpy(temp_buffer, track_buffer+(track*NIB_TRACK_LENGTH), track_length[track]);
				memcpy(temp_buffer+track_length[track], track_buffer+(track*NIB_TRACK_LENGTH), track_length[track]);

				track_length[track] = extract_GCR_track(
					track_buffer + (track * NIB_TRACK_LENGTH),
					temp_buffer,
					&track_alignment[track],
					track/2,
					capacity_min[track_density[track]&3],
					capacity_max[track_density[track]&3] );
			}
		}
	}
	if(verbose) printf("\n");
	return 1;
}

int align_tracks(BYTE *track_buffer, BYTE *track_density, size_t *track_length, BYTE *track_alignment)
{
	int track;
	BYTE nibdata[NIB_TRACK_LENGTH];

	memset(nibdata, 0, sizeof(nibdata));
	printf("Aligning tracks...\n");

	//for (track = start_track; track <= end_track; track ++)
	for (track = 1; track <= 84; track ++)
	{
		memcpy(nibdata, track_buffer+(track*NIB_TRACK_LENGTH), NIB_TRACK_LENGTH);
		memset(track_buffer + (track * NIB_TRACK_LENGTH), 0x00, NIB_TRACK_LENGTH);

		/* output some specs */
		if(verbose)
		{
			printf("%4.1f: ",(float) track/2);
			if(track_density[track] & BM_NO_SYNC) printf("NOSYNC! ");
			//if(track_density[track] & BM_FF_TRACK) printf("KILLER! ");
			printf("(D:%d) ", track_density[track]&3);
		}

		/* process track cycle */
		track_length[track] = extract_GCR_track(
			track_buffer + (track * NIB_TRACK_LENGTH),
			nibdata,
			&track_alignment[track],
			track/2,
			capacity_min[track_density[track]&3],
			capacity_max[track_density[track]&3]
		);

		/* output some specs */
		if(verbose)
		{
			printf("(L:%d) ", track_length[track]);
			printf("[align=%s]",alignments[track_alignment[track]]);
			printf("\n");
		}
	}
	return 1;
}

int rig_tracks(BYTE *track_buffer, BYTE *track_density, size_t *track_length, BYTE *track_alignment)
{
	int track;

	printf("Rigging tracks...\n");

	for (track = start_track; track <= end_track; track ++)
	{
		if(track_length[track]==0) continue;

		if(track_length[track] < capacity[track_density[track]&3])
		{
			memset(track_buffer + (track*NIB_TRACK_LENGTH) + track_length[track], 0x55, capacity[track_density[track]&3] - track_length[track]);
			//printf("Padded %d bytes\n", capacity[track_density[track]&3]-track_length[track]);
			track_length[track] = capacity[track_density[track]&3];
		}

		memcpy(track_buffer + (track*NIB_TRACK_LENGTH) + track_length[track],
			track_buffer + (track*NIB_TRACK_LENGTH), NIB_TRACK_LENGTH - track_length[track]);
	}
	return 1;


}

int compare_extension(unsigned char * filename, unsigned char * extension)
{
	unsigned char *dot;

	dot = strrchr(filename, '.');
	if (dot == NULL)
		return (0);

	for (++dot; *dot != '\0'; dot++, extension++)
		if (tolower(*dot) != tolower(*extension))
			return (0);

	if (*extension == '\0')
		return (1);
	else
		return (0);
}

int write_dword(FILE *fd, DWORD * buf, int num)
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

unsigned int crc_dir_track(BYTE *track_buffer, size_t *track_length)
{
	/* this calculates a CRC32 for the BAM and first directory sector, which is sufficient to differentiate most disks */

	unsigned char data[512];
	unsigned int result;
	BYTE id[3];
	BYTE rawdata[260];
	BYTE errorcode;

	crcInit();

	/* get disk id */
	if (!extract_id(track_buffer + (18 * 2 * NIB_TRACK_LENGTH), id))
	{
		printf("Cannot find directory sector.\n");
		return 0;
	}

	memset(data, 0, sizeof(data));

	/* t18s0 */
	memset(rawdata, 0, sizeof(rawdata));
	errorcode = convert_GCR_sector(
		track_buffer + ((18*2) * NIB_TRACK_LENGTH),
		track_buffer + ((18*2) * NIB_TRACK_LENGTH) + track_length[18*2],
		rawdata, 18, 0, id);
	memcpy(data, rawdata+1 , 256);

	/* t18s1 */
	memset(rawdata, 0, sizeof(rawdata));
	errorcode = convert_GCR_sector(
		track_buffer + ((18*2) * NIB_TRACK_LENGTH),
		track_buffer + ((18*2) * NIB_TRACK_LENGTH) + track_length[18*2],
		rawdata, 18, 1, id);
	memcpy(data+256, rawdata+1, 256);

	result = crcFast(data, sizeof(data));
	return result;
}

unsigned int crc_all_tracks(BYTE *track_buffer, size_t *track_length)
{
	/* this calculates a CRC32 for all sectors on the disk */

	unsigned char data[BLOCKSONDISK * 256];
	unsigned int result;
	int track, sector, index, valid;
	BYTE id[3];
	BYTE rawdata[260];
	BYTE errorcode;

	memset(data, 0, sizeof(data));
	crcInit();

	/* get disk id */
	if (!extract_id(track_buffer + (18*2 * NIB_TRACK_LENGTH), id))
	{
		printf("Cannot find directory sector.\n");
		return 0;
	}

	index = valid = 0;
	for (track = start_track; track <= 35*2; track += 2)
	{
		for (sector = 0; sector < sector_map[track/2]; sector++)
		{
			memset(rawdata, 0, sizeof(rawdata));

			errorcode = convert_GCR_sector(
				track_buffer + (track * NIB_TRACK_LENGTH),
				track_buffer + (track * NIB_TRACK_LENGTH) + track_length[track],
				rawdata, track/2, sector, id);

			memcpy(data+(index*256), rawdata+1, 256);
			index++;

			if(errorcode == SECTOR_OK)
				valid++;
		}
	}

	if(index != valid)
		if(verbose) printf("[%d/%d sectors] ", valid, index);

	result = crcFast(data, sizeof(data));
	return result;
}

unsigned int md5_dir_track(BYTE *track_buffer, size_t *track_length, unsigned char *result)
{
	/* this calculates a MD5 hash of the BAM and first directory sector, which is sufficient to differentiate most disks */

	unsigned char data[512];
	BYTE id[3];
	BYTE rawdata[260];
	BYTE errorcode;

	crcInit();
	memset(data, 0, sizeof(data));

	/* get disk id */
	if (!extract_id(track_buffer + (18*2 * NIB_TRACK_LENGTH), id))
	{
		printf("Cannot find directory sector.\n");
		return 0;
	}

	/* t18s0 */
	memset(rawdata, 0, sizeof(rawdata));
	errorcode = convert_GCR_sector(
		track_buffer + ((18*2) * NIB_TRACK_LENGTH),
		track_buffer + ((18*2) * NIB_TRACK_LENGTH) + track_length[18*2],
		rawdata, 18, 0, id);
	memcpy(data, rawdata+1 , 256);

	/* t18s1 */
	memset(rawdata, 0, sizeof(rawdata));
	errorcode = convert_GCR_sector(
		track_buffer + ((18*2) * NIB_TRACK_LENGTH),
		track_buffer + ((18*2) * NIB_TRACK_LENGTH) + track_length[18*2],
		rawdata, 18, 1, id);
	memcpy(data+256, rawdata+1, 256);

	md5(data, sizeof(data), result);
	return 1;
}

unsigned int md5_all_tracks(BYTE *track_buffer, size_t *track_length, unsigned char *result)
{
	/* this calculates an MD5 hash for all sectors on the disk */

	unsigned char data[BLOCKSONDISK * 256];
	int track, sector, index, valid;
	BYTE id[3];
	BYTE rawdata[260];
	BYTE errorcode;

	crcInit();
	memset(data, 0, sizeof(data));

	/* get disk id */
	if (!extract_id(track_buffer + (18*2 * NIB_TRACK_LENGTH), id))
	{
		printf("Cannot find directory sector.\n");
		return 0;
	}

	index = valid = 0;
	for (track = start_track; track <= 35*2; track += 2)
	{
		for (sector = 0; sector < sector_map[track/2]; sector++)
		{
			memset(rawdata, 0, sizeof(rawdata));

			errorcode = convert_GCR_sector(
				track_buffer + (track * NIB_TRACK_LENGTH),
				track_buffer + (track * NIB_TRACK_LENGTH) + track_length[track],
				rawdata, track/2, sector, id);

			memcpy(data+(index*256), rawdata+1, 256);
			index++;

			if(errorcode == SECTOR_OK)
				valid++;
		}
	}

	if(index != valid)
		if(verbose) printf("[%d/%d sectors] ", valid, index);

	md5(data, sizeof(data), result);
	return 1;
}




