/*
    NIBWRITE - part of the NIBTOOLS package for 1541/1571 disk image nibbling
	by Peter Rittwage <peter(at)rittwage(dot)com>
    based on code from MNIB, by Dr. Markus Brenner
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#include "mnibarch.h"
#include "gcr.h"
#include "version.h"
#include "nibtools.h"

char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };
char bitrate_value[4] = { 0x00, 0x20, 0x40, 0x60 };
char density_branch[4] = { 0xb1, 0xb5, 0xb7, 0xb9 };

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
int track_length[MAX_HALFTRACKS_1541 + 1];

int start_track, end_track, track_inc;
int start_track_override, end_track_override;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, aggressive_gcr;
int align, force_align;
unsigned int lpt[4];
int lpt_num;
int drivetype;
unsigned int floppybytes;
int imagetype;
int mode;
int verify;
int auto_capacity_adjust;
int align_disk;
int gap_match_length;
int cap_min_ignore;
int verbose = 0;
float motor_speed;
int skew = 0;

CBM_FILE fd;
FILE *fplog;

int ARCH_MAINDECL
main(int argc, char *argv[])
{
	BYTE drive = 8;
	int bump, reset, i;
	char filename[256];
	char argcache[256];

	fprintf(stdout,
	  "\nnibwrite - Commodore 1541/1571 disk image 'remastering' tool\n"
	  "(C) 2007 Peter Rittwage\n" "Version " VERSION "\n\n");

	/* we can do nothing with no switches */
	if (argc < 2)	usage();

	if(!(track_buffer = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate memory for buffers.\n");
		exit(0);
	}

#ifdef DJGPP
	fd = 1;
#endif

	bump = 1;  /* failing to bump sometimes give us wrong tracks on heavily protected disks */
	reset = 1;

	start_track =  2;
	end_track = 82;
	start_track_override = 0;
	end_track_override = 0;
	track_inc = 2;

	reduce_sync = 1;
	reduce_badgcr = 0;
	reduce_gap = 0;
	fix_gcr = 1;
	align_disk = 0;
	auto_capacity_adjust = 1;
	verbose = 0;
	gap_match_length = 7;
	cap_min_ignore = 0;
	motor_speed = 300;

	mode = MODE_WRITE_DISK;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;

	// cache our arguments for logfile generation
	strcpy(argcache, "");
	for (i = 0; i < argc; i++)
	{
		strcat(argcache, argv[i]);
		strcat(argcache," ");
	}

	// parse arguments
	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{
		case 'h':
			track_inc = 1;
			//start_track = 1;  /* my drive knocks on this track - PJR */
			end_track = 83;
			printf("* Using halftracks\n");
			break;

		case 'B':
		case 'S':
			if (!(*argv)[2]) usage();
			start_track_override = (BYTE) (2 * (atoi((char *) (&(*argv)[2]))));
			printf("* Start track set to %d\n", start_track_override/2);
			break;

		case 'l':
		case 'E':
			if (!(*argv)[2]) usage();
			end_track_override = (BYTE) (2 * (atoi((char *) (&(*argv)[2]))));
			printf("* End track set to %d\n", end_track_override/2);
			break;

		case 't':
			align_disk = 1;
			printf("* Attempt soft track alignment\n");
			break;

		case 'u':
			mode = MODE_UNFORMAT_DISK;
			break;

		case 'R':
			// hidden secret raw track file writing mode
			printf("* Raw track dump write mode\n");
			mode = MODE_WRITE_RAW;
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
				align_disk = 1;
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
			else if ((*argv)[2] == 'g')
			{
				printf("gap\n");
				force_align = ALIGN_GAP;
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
			else
				printf("Unknown alignment parameter\n");
			break;

		case 'r':
			reduce_sync = 0;
			printf("* Disabled 'reduce sync' option\n");
			break;

		case 'D':
			if (!(*argv)[2])
				usage();
			drive = (BYTE) atoi((char *) (&(*argv)[2]));
			printf("* Use Device %d\n", drive);
			break;

		case '0':
			reduce_badgcr = 1;
			printf("* Enabled 'reduce bad GCR' option\n");
			break;

		case 'g':
			reduce_gap = 1;
			printf("* Enabled 'reduce gaps' option\n");
			break;

		case 'G':
			if (!(*argv)[2])
				usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'f':
			fix_gcr = 0;
			printf("* Disabled bad GCR bit reproduction\n");
			break;

		case 'v':
			verbose = 1;
			printf("* Verbose mode on\n");
			break;

		case 'm':
			auto_capacity_adjust = 0;
			printf("* Disabled automatic capacity adjustment\n");
			break;

		case 'c':
			printf("* Minimum capacity ignore on\n");
			cap_min_ignore = 1;
			break;

		case 's':
			if (!(*argv)[2])
				usage();
			align_disk = 1;
			printf("* Attempt soft track alignment\n");
			skew = atoi((char *) (&(*argv)[2]));
			printf("* Skew set to %dus\n",skew);
			break;

		default:
			usage();
			break;
		}
	}
	printf("\n");

	if (argc > 0)	strcpy(filename, argv[0]);

#ifdef DJGPP
	calibrate();
	if (!detect_ports(reset))
		return 0;
#else
	/* under Linux we have to open the device via cbm4linux */
	cbm_driver_open(&fd, 0);
#endif

	/* Once the drive is accessed, we need to close out state when exiting */
	atexit(handle_exit);
	signal(SIGINT, handle_signals);

	if(filename)
	{
		if(!loadimage(filename))
		{
			printf("\nImage loading failed\n");
			exit(0);
		}
	}

	if(!init_floppy(fd, drive, bump))
	{
		printf("\nFloppy drive initialization failed\n");
		exit(0);
	}

	switch (mode)
	{
		case MODE_WRITE_DISK:
			//printf("Ready to write '%s'.\n", filename);
			//printf("Current disk WILL be OVERWRITTEN!\n"
			//	"Press ENTER to continue or CTRL-C to quit.\n");
			//getchar();
			writeimage(fd);
			break;

		case MODE_UNFORMAT_DISK:
			//printf("Ready to unformat disk.\n");
			//printf("Current disk WILL be DESTROYED!\n"
			//  "Press ENTER to continue or CTRL-C to quit.\n");
			//getchar();
			unformat_disk(fd);
			break;

		case MODE_WRITE_RAW:
			//printf("Ready to write raw tracks to disk.\n");
			//printf("Current disk WILL be OVERWRITTEN!\n"
			//  "Press ENTER to continue or CTRL-C to quit.\n");
			//getchar();
			write_raw(fd, track_buffer, track_density, track_length);
			break;
	}

	motor_on(fd);
	step_to_halftrack(fd, 18 * 2);

	exit(0);
}

int
loadimage(char * filename)
{
	char command[256];
	char pathname[256];
	char *dotpos, *pathpos;
	int iszip = 0;
	int retval = 1;

	/* unzip image if possible */
	if (compare_extension(filename, "ZIP"))
	{
		printf("Unzipping image...\n");
		dotpos = strrchr(filename, '.');
		if (dotpos != NULL) *dotpos = '\0';
		strcpy(pathname, filename);
		pathpos = strrchr(pathname, '\\');
		if (pathpos != NULL) *pathpos = '\0';
		sprintf(command, "unzip %s.zip -d %s", filename, pathname);
		system(command);
		iszip++;
	}

	/* clear our buffers */
	memset(track_buffer, 0, sizeof(track_buffer));
	memset(track_density, 0, sizeof(track_density));

	/* read and remaster disk */
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
	{
		printf("\nUnknown image type");
		retval = 0;
	}

	if(iszip)
	{
		unlink(filename);
		printf("Temporary file deleted.\n");
	}

	return retval;
}

int writeimage(CBM_FILE fd)
{
	track_inc = 2;  /* 15x1 can't write halftracks */
	if(start_track_override) start_track = start_track_override;
	if(end_track_override) end_track = end_track_override;

	/* turn on motor and measure speed */
	motor_on(fd);

	/* prepare fisk for writing */
	if(auto_capacity_adjust) adjust_target(fd);
	if(align_disk) init_aligned_disk(fd);

	master_disk(fd, track_buffer, track_density, track_length);
	step_to_halftrack(fd, 18 * 2);
	cbm_parallel_burst_read(fd);
	printf("\n");

	return 1;
}

void
usage(void)
{
	fprintf(stderr, "usage: nibwrite [options] <filename>\n\n"
	     " -D[n[: Use drive #[n]\n"
	     " -S[n]: Override starting track\n"
	     " -E[n]: Override ending track\n"
	     " -a[x]: Force alternative track alignments (advanced users only)\n"
	     " -p[x]: Custom protection handlers (advanced users only)\n"
	     " -s[n]: Manual track skew (in microseconds)\n"
	     " -t: Enable timer-based track alignment\n"
	     " -g: Enable gap reduction\n"
	     " -0: Enable bad GCR run reduction\n"
	     " -r: Disable automatic sync reduction\n"
	     " -c: Disable automatic capacity adjustment\n"
	     " -f: Disable automatic bad GCR detection\n"
	     " -u: Unformat disk. (writes all 0 bits to surface)\n"
	     " -v: Verbose (output more detailed track data)\n"
	     " -G: Manual gap match length\n"
	     );
	exit(1);
}

