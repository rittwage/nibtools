/*
    NIBREAD - part of the NIBTOOLS package for 1541/1571 disk image nibbling
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
#include "nibtools.h"

int _dowildcard = 1;

char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };
char bitrate_value[4] = { 0x00, 0x20, 0x40, 0x60 };
char density_branch[4] = { 0xb1, 0xb5, 0xb7, 0xb9 };

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
int track_length[MAX_HALFTRACKS_1541 + 1];

int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, aggressive_gcr;
int start_track, end_track, track_inc;
int read_killer;
int align;
int error_retries;
int drivetype;
int imagetype;
int mode;
int force_density;
int track_match;
int gap_match_length;
int cap_min_ignore;
int interactive_mode;
int verbose;
int extended_parallel_test;
int force_nosync;
int ihs;
int drive;
int auto_capacity_adjust;
int align_disk;
int skew;
int rawmode;
BYTE fillbyte;
int rpm_real;

BYTE density_map;
float motor_speed;

CBM_FILE fd;
FILE *fplog;

int ARCH_MAINDECL
main(int argc, char *argv[])
{
	BYTE drive = 8;
	int bump, reset, i;
	char filename[256], logfilename[256], *dotpos;
	char argcache[256];

	fprintf(stdout,
	  "\nnibread - Commodore 1541/1571 disk image nibbler\n"
	  "(C) C64 Preservation Project\nhttp://c64preservation.com\n" "Version " VERSION "\n\n");

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

	start_track = 1 * 2;
	end_track = 41 * 2;
	track_inc = 2;

	reduce_sync = 1;
	reduce_badgcr = 0;
	reduce_gap = 0;
	fix_gcr = 1;
	read_killer = 1;
	error_retries = 10;
	force_density = 0;
	track_match = 0;
	interactive_mode = 0;
	verbose = 0;
	extended_parallel_test = 0;
	force_nosync = 0;
	align = ALIGN_NONE;
	gap_match_length = 7;
	cap_min_ignore = 0;
	ihs = 0;
	mode = MODE_READ_DISK;
	fillbyte = 0x55;
	rawmode = 0;
	rpm_real = 0;

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
			end_track = 83;
			printf("* Using halftracks\n");
			break;

		case 'r':
			rawmode = 1;
			printf("* Using raw mode\n");
			break;

		case 'I':
			printf("* 1571 index hole sensor (use only for side 1)\n");
			ihs = 1;
			break;

		case 'v':
			track_match = 1;
			printf("* Simple track match (crude verify)\n");
			break;

		case 's':
			force_nosync = 1;
			printf("* Forcing read without regard to sync\n");
			break;

		case 't':
			extended_parallel_test = 1;
			printf("* Extended parallel port testing\n");
			break;

		case 'i':
			interactive_mode = 1;
			printf("* Interactive mode\n");
			break;

		case 'd':
			force_density = 1;
			printf("* Forcing default density\n");
			break;

		case 'k':
			read_killer = 0;
			printf("* Disabling read of 'killer' tracks\n");
			break;

		case 'S':
			if (!(*argv)[2]) usage();
			start_track = (BYTE) (2 * (atoi((char *) (&(*argv)[2]))));
			printf("* Start track set to %d\n", start_track/2);
			break;

		case 'E':
			if (!(*argv)[2]) usage();
			end_track = (BYTE) (2 * (atoi((char *) (&(*argv)[2]))));
			printf("* End track set to %d\n", end_track/2);
			break;

		case 'D':
			if (!(*argv)[2]) usage();
			drive = (BYTE) atoi((char *) (&(*argv)[2]));
			printf("* Use Device %d\n", drive);
			break;

		case 'G':
			if (!(*argv)[2]) usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'V':
			verbose = 1;
			printf("* Verbose mode on\n");
			break;

		case 'e':	// change read retries
			if (!(*argv)[2]) usage();
			error_retries = atoi((char *) (&(*argv)[2]));
			printf("* Read retries set to %d\n", error_retries);
			break;

		case 'm':
			printf("* Minimum capacity ignore on\n");
			cap_min_ignore = 1;
			break;

		default:
			usage();
			break;
		}
	}
	printf("\n");

	if (argc < 1)	usage();
	strcpy(filename, argv[0]);

	/* create log file */
	strcpy(logfilename, filename);
	dotpos = strrchr(logfilename, '.');
	if (dotpos != NULL)
		*dotpos = '\0';
	strcat(logfilename, ".log");

	if ((fplog = fopen(logfilename, "wb")) == NULL)
	{
		fprintf(stderr, "Couldn't create log file %s!\n", logfilename);
		exit(2);
	}

	fprintf(fplog, "%s\n", VERSION);
	fprintf(fplog, "'%s'\n", argcache);

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

	if(!init_floppy(fd, drive, bump))
	{
		printf("Floppy drive initialization failed\n");
		exit(0);
	}

	if(extended_parallel_test)
	{
		printf("Performing extended parallel port test\n");
		for(i=0; i<100; i++)
		{
			if(!verify_floppy(fd))
			{
				printf("Parallel port failed extended testing.  Check wiring and sheilding.\n");
				exit(0);
			}
			printf(".");
		}
		printf("\nPassed advanced parallel port test\n");
	}

	if(strrchr(filename, '.') == NULL)  strcat(filename, ".nib");
	disk2file(fd, filename);

	motor_on(fd);
	step_to_halftrack(fd, 18 * 2);

	if(fplog) fclose(fplog);
	exit(0);
}

int disk2file(CBM_FILE fd, char *filename)
{
	int count = 0;
	char newfilename[256];
	char filenum[4], *dotpos;

	/* read data from drive to file */
	motor_on(fd);

	if (compare_extension(filename, "NB2"))
	{
		track_inc = 1;
		write_nb2(fd, filename);
	}
	else
	{
		read_floppy(fd, track_buffer, track_density, track_length);
		write_nib(filename, track_buffer, track_density, track_length);

		if(interactive_mode)
		{
			for(;;)
			{
				printf("Swap disk and press a key for next image, or CTRL-C to quit.\n");
				getchar();

				/* create new filename */
				sprintf(filenum, "%d", ++count);
				strcpy(newfilename, filename);
				dotpos = strrchr(newfilename, '.');
				if (dotpos != NULL) *dotpos = '\0';
				strcat(newfilename, filenum);
				strcat(newfilename, ".nib");
				read_floppy(fd, track_buffer, track_density, track_length);
				write_nib(newfilename, track_buffer, track_density, track_length);
			}
		}
	}

	cbm_parallel_burst_read(fd);
	return (0);
}

void
usage(void)
{
	fprintf(stderr, "usage: nibread [options] <filename>\n\n"
	     " -D[n]: Use drive #[n]\n"
	     " -e[n]: Retry reading tracks with errors [n] times\n"
	     " -S[n]: Override starting track\n"
	     " -E[n]: Override ending track\n"
	     " -G[n]: Match track gap by [n] number of bytes (advanced users only)\n"
	     " -k: Disable reading of 'killer' tracks\n"
	     " -d: Force default densities\n"
	     " -v: Enable track matching (crude read verify)\n"
	     " -i: Interactive imaging mode\n"
	     " -m: Disable minimum capacity check\n"
	     " -V: Verbose (output more detailed track data)\n"
	     " -h: Read halftracks\n"
	     " -t: Extended parallel port tests\n"
	     );
	exit(1);
}
