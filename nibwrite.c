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

BYTE start_track, end_track, track_inc;
int reduce_syncs, reduce_weak, reduce_gaps;
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
int verbose;
float motor_speed;

CBM_FILE fd;

int ARCH_MAINDECL
main(int argc, char *argv[])
{
	BYTE drive = 8;
	int bump, reset, i;
	char cmd[80], error[500], filename[256];
	char argcache[256];

	fprintf(stdout,
	  "\nnibwrite - Commodore 1541/1571 disk image 'remastering' tool\n"
	  "(C) 2007 Peter Rittwage\n" "Version " VERSION "\n\n");

	/* we can do nothing with no switches */
	if (argc < 2)	usage();

#ifdef DJGPP
	fd = 1;
#endif
	bump = reset = 1;	// by default, use reset, bump

	start_track = 2;
	end_track = 82;
	track_inc = 2;

	reduce_syncs = 1;
	reduce_weak = 0;
	reduce_gaps = 0;
	fix_gcr = 1;
	align_disk = 0;
	auto_capacity_adjust = 1;
	verbose = 0;
	gap_match_length = 7;
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

		case 't':
			align_disk = 1;
			printf("* Timer-based track alignment\n");
			break;

		case 'u':
			mode = MODE_UNFORMAT_DISK;
			break;

		case 'R':
			// hidden secret raw track file writing mode
			printf("* Raw pscan track dump\n");
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
			else if ((*argv)[2] == 'g')
			{
				printf("GMA/SecuriSpeed\n");
				reduce_syncs = 0;
				reduce_weak = 1;
			}
			else if ((*argv)[2] == 'v')
			{
				printf("VORPAL (NEWER)\n");
				force_align = ALIGN_AUTOGAP;
			}
			else if ((*argv)[2] == 'r')
			{
				printf("RAPIDLOK\n");
				reduce_syncs = 1;
				reduce_weak = 1;
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
				printf("longest weak run\n");
				force_align = ALIGN_WEAK;
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
			reduce_syncs = 0;
			printf("* Disabled 'reduce syncs' option\n");
			break;

		case 'D':
			if (!(*argv)[2])
				usage();
			drive = (BYTE) atoi((char *) (&(*argv)[2]));
			printf("* Use Device %d\n", drive);
			break;

		case '0':
			reduce_weak = 1;
			printf("* Enabled 'reduce weak' option\n");
			break;

		case 'g':
			reduce_gaps = 1;
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
			printf("* Disabled weak GCR bit simulation\n");
			break;

		case 'V':
			verbose = 1;
			printf("* Verbose mode on\n");
			break;

		case 'c':
			auto_capacity_adjust = 0;
			printf("* Disabled automatic capacity adjustment\n");
			break;

		default:
			usage();
			break;
		}
	}
	printf("\n");

	if (mode == MODE_WRITE_DISK)
	{
		if (argc < 1)
			usage();
		else
			strcpy(filename, argv[0]);
	}

#ifdef DJGPP
	calibrate();

	if (!detect_ports(reset))
		exit(3);
#else
	/* under Linux we have to open the device via cbm4linux */
	cbm_driver_open(&fd, 0);
#endif

	/* Once the drive is accessed, we need to close out state when exiting */
	atexit(handle_exit);
	signal(SIGINT, handle_signals);

	/* prepare error string $73: CBM DOS V2.6 1541 */
	sprintf(cmd, "M-W%c%c%c%c%c%c%c%c", 0, 3, 5, 0xa9, 0x73, 0x4c, 0xc1, 0xe6);
	cbm_exec_command(fd, drive, cmd, 11);
	sprintf(cmd, "M-E%c%c", 0x00, 0x03);
	cbm_exec_command(fd, drive, cmd, 5);
	cbm_device_status(fd, drive, error, sizeof(error));
	printf("Drive Version: %s\n", error);

	if (error[18] == '7')
		drivetype = 1571;
	else
		drivetype = 1541;	/* if unknown drive, use 1541 code */

	printf("Drive type: %d\n", drivetype);

	delay(1000);

	if (bump)
		perform_bump(fd,drive);

	/*
	 * Initialize media and switch drive to 1541 mode.
	 * We initialize first to do the head seek to read the BAM after
	 * the bump above.
	 * Changed to perform the 1541 mode select first, or else it breaks on a 1571 (PR)
	 */
	printf("Initializing\n");
	cbm_exec_command(fd, drive, "U0>M0", 0);
	cbm_exec_command(fd, drive, "I0", 0);

	if (upload_code(fd, drive) < 0) {
		printf("code upload failed, exiting\n");
		exit(5);
	}

	/* Begin executing drive code at $300 */
	sprintf(cmd, "M-E%c%c", 0x00, 0x03);
	cbm_exec_command(fd, drive, cmd, 5);

	cbm_parallel_burst_read(fd);

#ifdef DJGPP
	if (!find_par_port(fd)) {
		exit(6);
	}
#endif

	if(!test_par_port(fd))
	{
		printf("\nFailed parallel port transfer test. Check cabling.\n");
		exit(7);
	}
	if(!verify_floppy(fd))
	{
		printf("\nFailed parallel port transfer test. Check cabling.\n");
		exit(7);
	}

	switch (mode)
	{
	case MODE_WRITE_DISK:
		//printf("Ready to write '%s'.\n", filename);
		//printf("Current disk WILL be OVERWRITTEN!\n"
		//	"Press ENTER to continue or CTRL-C to quit.\n");
		//getchar();
		file2disk(fd, filename);
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
		write_raw(fd);
		break;
	}

	motor_on(fd);
	step_to_halftrack(fd, 18 * 2);

	exit(0);
}

int
file2disk(CBM_FILE fd, char * filename)
{
	FILE *fpin;
	char mnibheader[0x100], g64header[0x2ac];
	int nibsize, numtracks;

	if ((fpin = fopen(filename, "rb")) == NULL)
	{
		fprintf(stderr, "Couldn't open input file %s!\n", filename);
		exit(2);
	}

	motor_on(fd);

	if (auto_capacity_adjust)
		adjust_target(fd);

	/* this is deprecated, we can do it during the write */
	//if (align_disk)
	//	init_aligned_disk(fd);

	if (compare_extension(filename, "D64"))
	{
		imagetype = IMAGE_D64;
		write_d64(fd, fpin);
	}
	else if (compare_extension(filename, "G64"))
	{
		imagetype = IMAGE_G64;
		memset(g64header, 0x00, sizeof(g64header));

		if (fread(g64header, sizeof(g64header), 1, fpin) != 1) {
			printf("unable to read G64 header\n");
			exit(2);
		}
		parse_disk(fd, fpin, g64header + 0x9);
	}
	else if (compare_extension(filename, "NIB"))
	{
		imagetype = IMAGE_NIB;

		/* Determine number of tracks in image (crude) */
		fseek(fpin, 0, SEEK_END);
		nibsize = ftell(fpin);
		numtracks = (nibsize - 0xff) / 8192;

		if(numtracks <= 42)
		{
			end_track = (numtracks * 2) + 1;
			track_inc = 2;
		}
		else
		{
			printf("\nImage contains halftracks!\n");
			end_track = numtracks + 1;
			track_inc = 1;
		}

		printf("\n%d track image (filesize = %d bytes)\n", numtracks, nibsize);
		rewind(fpin);

		/* gather mem and read header */
		memset(mnibheader, 0x00, sizeof(mnibheader));
		if (fread(mnibheader, sizeof(mnibheader), 1, fpin) != 1) {
			printf("unable to read NIB header\n");
			exit(2);
		}
		parse_disk(fd, fpin, mnibheader + 0x10);
	}
	else
		printf("\nUnknown image type");

	printf("\n");
	cbm_parallel_burst_read(fd);

	fclose(fpin);
	return (0);
}

void
usage(void)
{
	fprintf(stderr, "usage: nibwrite [options] <filename>\n\n"
	     " -D[n[: Use drive #[n]\n"
	     " -a[x]: Force alternative track alignments (advanced users only)\n"
	     " -p[x]: Custom protection handlers (advanced users only)\n"
	     " -t: Enable timer-based track alignment\n"
	     " -g: Enable gap reduction\n"
	     " -0: Enable weak-bit run reduction\n"
	     " -r: Disable automatic sync reduction\n"
	     " -c: Disable automatic capacity adjustment\n"
	     " -f: Disable automatic weak GCR bit simulation\n"
	     " -u: Unformat disk. (writes all 0 bits to surface)\n"
	     " -V: Verbose (output more detailed track data)\n"
	     " -G: Gap match length\n"
	     );
	exit(1);
}

