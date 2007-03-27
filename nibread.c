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
#include "version.h"
#include "nibtools.h"

char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };
char bitrate_value[4] = { 0x00, 0x20, 0x40, 0x60 };
char density_branch[4] = { 0xb1, 0xb5, 0xb7, 0xb9 };

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
int track_length[MAX_HALFTRACKS_1541 + 1];

int reduce_syncs, reduce_weak, reduce_gaps;
int fix_gcr, aggressive_gcr;
int start_track, end_track, track_inc;
int read_killer;
int align, force_align;
int error_retries;
int drivetype;
int imagetype;
int mode;
int force_density;
int track_match;
int gap_match_length;
int interactive_mode;
int verbose;
int density_map;
int extended_parallel_test;
int force_nosync;
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
	  "(C) 2007 Peter Rittwage, Dr. Markus Brenner\n" "Version " VERSION "\n\n");

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
	bump = reset = 1;	// by default, use reset, bump

	start_track = 1 * 2;
	end_track = 41 * 2;
	track_inc = 2;

	reduce_syncs = 1;
	reduce_weak = 0;
	reduce_gaps = 0;
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
	force_align = ALIGN_NONE;
	gap_match_length = 7;
	mode = MODE_READ_DISK;
	density_map = DENSITY_STANDARD;

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

		case 'm':
			track_match = 1;
			printf("* Simple track match\n");
			break;

		case 's':
			force_nosync = 1;
			printf("* Force reading without regard to sync\n");
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
			density_map = DENSITY_STANDARD;
			printf("* Force default density\n");
			break;

		case 'k':
			read_killer = 0;
			printf("* Disable reading of 'killer' tracks\n");
			break;

		case 'l':
			if (!(*argv)[2]) usage();
			end_track = (BYTE) (2 * (atoi((char *) (&(*argv)[2]))));
			printf("* Limiting functions to %d tracks\n", end_track/2);
			break;

		case 'B':
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

		case 'p':
			// custom protection handling
			printf("* Custom copy protection handler: ");
			if ((*argv)[2] == 'r')
			{
				printf("RAPIDLOK\n");
				force_density = 1;
				density_map = DENSITY_RAPIDLOK;
				error_retries = 1;
			}
			else
				printf("Unknown protection handler\n");
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
		printf("Performing advanced parallel port test\n");
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

	if (compare_extension(filename, "D64"))
	{
		//read_floppy(fd, track_buffer, track_density, track_length);
		//write_d64(filename, track_buffer, track_density, track_length, 1);
		printf("\nWARNING!\nReading to D64 is a lossy conversion.\n");
		printf("All individual sector header and gap information is lost.\n");
		printf("It is suggested you use the NIB format for archival.\n");
	}
	else if (compare_extension(filename, "G64"))
	{
		//read_floppy(fd, track_buffer, track_density, track_length);
		//write_g64(filename, track_buffer, track_density, track_length, 1);
		printf("\nWARNING!\nReading to G64 is a slightly lossy conversion.\n");
		printf("Some track cycle information is lost.\n");
		printf("It is suggested you use the NIB format for archival.\n");
	}
	else if (compare_extension(filename, "NB2"))
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
	     " -l[n]: Limit functions to [n] tracks. (advanced users only)\n"
	     " -G[n]: Match track gap by [n] number of bytes (advanced users only)\n"
	     " -k: Disable reading of 'killer' tracks\n"
	     " -d: Force default densities\n"
	     " -m: Enable track matching (crude read verify)\n"
	     " -i: Interactive imaging mode\n"
	     " -V: Verbose (output more detailed track data)\n"
	     " -h: Read halftracks\n"
	     " -t: Extended parallel port tests\n"
	     );
	exit(1);
}
