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
#include "lz.h"

int _dowildcard = 1;

char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };
char bitrate_value[4] = { 0x00, 0x20, 0x40, 0x60 };
char density_branch[4] = { 0xb1, 0xb5, 0xb7, 0xb9 };

BYTE *file_buffer;
BYTE *compressed_buffer;
BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
size_t track_length[MAX_HALFTRACKS_1541 + 1];

size_t error_retries;
int file_buffer_size;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr;
int start_track, end_track, track_inc;
int read_killer;
int align;
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
int rpm_real;
int unformat_passes;
int capacity_margin;
int align_delay;
int align_report;
int increase_sync = 0;
int presync = 0;
BYTE fillbyte = 0x55;

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
	FILE *fp;

	fprintf(stdout,
	  "\nnibread - Commodore 1541/1571 disk image nibbler\n"
	  "(C) 2004-2010 Peter Rittwage\nC64 Preservation Project\nhttp://c64preservation.com\n"
	  "Revision %d - " VERSION "\n\n", SVN);

	/* we can do nothing with no switches */
	if (argc < 2)	usage();

	if(!(file_buffer = calloc(MAX_HALFTRACKS_1541 + 2, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate buffer memory\n");
		exit(0);
	}

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
	fix_gcr = 0;
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
	rpm_real = 0;
	align_report = 0;

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

		case 'A':
			align_report = 1;
			printf("* Track Alignment Report\n");
			break;

		case 'h':
			track_inc = 1;
			end_track = 83;
			printf("* Using halftracks\n");
			break;

		case 'i':
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
			extended_parallel_test = atoi(&(*argv)[2]);
			if(!extended_parallel_test)
				extended_parallel_test = 100;
			printf("* Extended parallel port test loops = %d\n", extended_parallel_test);
			break;

		case 'I':
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
			start_track = (BYTE) (2*(atoi(&(*argv)[2])));
			printf("* Start track set to %d\n", start_track/2);
			break;

		case 'E':
			if (!(*argv)[2]) usage();
			end_track =  (BYTE) (2*(atoi(&(*argv)[2])));
			printf("* End track set to %d\n", end_track/2);
			break;

		case 'D':
			if (!(*argv)[2]) usage();
			drive = (BYTE) (atoi(&(*argv)[2]));
			printf("* Use Device %d\n", drive);
			break;

		case 'G':
			if (!(*argv)[2]) usage();
			gap_match_length = atoi(&(*argv)[2]);
			printf("* Gap match length set to %d\n", gap_match_length);
			break;

		case 'V':
			verbose++;
			printf("* Verbose mode on\n");
			break;

		case 'e':	// change read retries
			if (!(*argv)[2]) usage();
			error_retries = atoi(&(*argv)[2]);
			printf("* Read retries set to %lu\n", error_retries);
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


#ifdef DJGPP
	calibrate();
	if (!detect_ports(reset))
		return 0;
#else
	/* under Linux we have to open the device via cbm4linux */
	if (cbm_driver_open(&fd, 0) != 0) {
		printf("Is your X-cable properly configured?\n");
		exit(0);
	}
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
		parallel_test(extended_parallel_test);

	if(align_report)
		TrackAlignmentReport(fd);

	if(argc < 1) usage();
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

	if(strrchr(filename, '.') == NULL)  strcat(filename, ".nbz");

	if((compare_extension(filename, "D64")) || (compare_extension(filename, "G64")))
	{
		printf("\nDisk imaging only directly supports NIB, NB2, and NBZ formats.\n");
		printf("Use nibconv after imaging to convert to desired file type.\n");
		exit(0);
	}

	if( (fp=fopen(filename,"r")) )
	{
		fclose(fp);
		printf("File exists - Overwrite? (y/N)");
		if(getchar() != 'y') exit(0);
	}


	if(!(disk2file(fd, filename)))
		printf("Operation failed!\n");

	motor_on(fd);
	step_to_halftrack(fd, 18*2);

	if(fplog) fclose(fplog);
	free(file_buffer);
	free(track_buffer);
	exit(0);
}

void parallel_test(int iterations)
{
	int i;

	printf("Performing extended parallel port test\n");
	for(i=0; i<iterations; i++)
	{
		if(!verify_floppy(fd))
		{
			printf("Parallel port failed extended testing.  Check wiring and sheilding.\n");
			exit(0);
		}
		printf(".");
	}
	printf("\nPassed advanced parallel port test\n");
	exit(0);
}

int disk2file(CBM_FILE fd, char *filename)
{
	int count = 0;
	char newfilename[256];
	char filenum[4], *dotpos;

	/* read data from drive to file */
	motor_on(fd);

	if(compare_extension(filename, "NB2"))
	{
		track_inc = 1;
		if(!(write_nb2(fd, filename))) return 0;
	}
	else if(compare_extension(filename, "NIB"))
	{
		if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
		if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
		if(!(save_file(filename, file_buffer, file_buffer_size))) return 0;

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

				if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
				if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
				if(!(save_file(newfilename, file_buffer, file_buffer_size))) return 0;
			}
		}
	}
	else
	{
		if(!(compressed_buffer = calloc(MAX_HALFTRACKS_1541+2, NIB_TRACK_LENGTH)))
		{
			printf("could not allocate buffer memory\n");
			exit(0);
		}

		if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
		if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
		if(!(file_buffer_size = LZ_CompressFast(file_buffer, compressed_buffer, file_buffer_size))) return 0;
		if(!(save_file(filename, compressed_buffer, file_buffer_size))) return 0;

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
				strcat(newfilename, ".nbz");

				if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
				if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
				if(!(file_buffer_size = LZ_CompressFast(file_buffer, compressed_buffer, file_buffer_size))) return 0;
				if(!(save_file(newfilename, compressed_buffer, file_buffer_size))) return 0;
			}
		}
			free(compressed_buffer);
	}
	cbm_parallel_burst_read(fd);
	return 1;
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
	     " -I: Interactive imaging mode\n"
	     " -m: Disable minimum capacity check\n"
	     " -V: Verbose (output more detailed track data)\n"
	     " -h: Read halftracks\n"
	     " -t: Extended parallel port tests\n"
	     );
	exit(1);
}
