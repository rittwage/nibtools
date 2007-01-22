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

BYTE start_track, end_track, track_inc;
int read_killer;
int align, force_align;
int error_retries;
int drivetype;
int imagetype;
int mode;
int default_density;
int track_match;
int gap_match_length;
int interactive_mode;
int verbose;
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

#ifdef DJGPP
	fd = 1;
#endif
	bump = reset = 1;	// by default, use reset, bump

	start_track = 2;
	end_track = 82;
	track_inc = 2;

	read_killer = 1;
	error_retries = 10;
	default_density = 0;
	track_match = 0;
	interactive_mode = 0;
	verbose = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;
	mode = MODE_READ_DISK;

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

		case 'm':
			track_match = 1;
			printf("* Simple track match\n");
			break;

		case 'i':
			interactive_mode = 1;
			printf("* Interactive mode\n");
			break;

		case 'd':
			default_density = 1;
			printf("* Force default density\n");
			break;

		case 'k':
			read_killer = 0;
			printf("* Disable reading of 'killer' tracks\n");
			break;

		case 'l':
			if (!(*argv)[2])
				usage();
			end_track = (BYTE) (2 * (atoi((char *) (&(*argv)[2]))));
			printf("* Limiting functions to %d tracks\n", end_track/2);
			break;

		case 'D':
			if (!(*argv)[2])
				usage();
			drive = (BYTE) atoi((char *) (&(*argv)[2]));
			printf("* Use Device %d\n", drive);
			break;

		case 'G':
			if (!(*argv)[2])
				usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'V':
			verbose = 1;
			printf("* Verbose mode on\n");
			break;

		case 'e':	// change read retries
			if (!(*argv)[2])
				usage();
			error_retries = atoi((char *) (&(*argv)[2]));
			printf("* Read retries set to %d\n", error_retries);
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

	disk2file(fd, filename);

	motor_on(fd);
	step_to_halftrack(fd, 18 * 2);

	if(fplog) fclose(fplog);

	exit(0);
}

int disk2file(CBM_FILE fd, char *filename)
{
	int filenum = 0;
	char * seqname;

	if (compare_extension(filename, "D64"))
		imagetype = IMAGE_D64;
	else if (compare_extension(filename, "G64"))
		imagetype = IMAGE_G64;
	else if (compare_extension(filename, "NB2"))
		imagetype = IMAGE_NB2;
	else
		imagetype = IMAGE_NIB;

	if (imagetype == IMAGE_G64)
	{
		printf("You cannot dump directly to G64 file format.  Use NIB or NB2 instead.");
		exit(2);
	}

	/* read data from drive to file */
	motor_on(fd);

	if (imagetype == IMAGE_NIB)
	{
		while(interactive_mode || filenum == 0)
		{
			if(strrchr(filename, '.') == NULL)  strcat(filename, ".nib");
			read_nib(fd, filename);
			filenum++;

			if(interactive_mode)
			{
				printf("\nPress enter to image next side with automatic filename,\n");
				printf("'f' to enter new filename, or 'q' to quit.\n:");

				fflush(stdin);
				switch(getchar())
				{
					case 'f':
						printf("Enter new filename:");
						scanf("%s",filename);
						break;

					case 'q':
						interactive_mode = 0;
						break;

					default:
						seqname = strtok (filename,".");
						sprintf(filename, "%s%d", seqname, filenum+1);
						break;
				}
			}
		}
	}
	else if (imagetype == IMAGE_NB2)
	{
		track_inc = 1;
		read_nb2(fd, filename);
	}
	else
	{
		read_killer = 0;  // no need to try this on a D64
		read_d64(fd, filename);
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
	     );
	exit(1);
}
