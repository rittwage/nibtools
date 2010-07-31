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
#include "nibtools.h"

int _dowildcard = 1;

char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };
char bitrate_value[4] = { 0x00, 0x20, 0x40, 0x60 };
char density_branch[4] = { 0xb1, 0xb5, 0xb7, 0xb9 };

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
size_t track_length[MAX_HALFTRACKS_1541 + 1];

int start_track, end_track, track_inc;
int reduce_sync;
int fix_gcr, aggressive_gcr;
int align;
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
int ihs = 0;
int drive;
int rpm_real;
int unformat_passes;
int align_delay;

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
	  "(C) 2004-2010 Peter Rittwage\nC64 Preservation Project\nhttp://c64preservation.com\n" "Version " VERSION "\n\n");

	/* we can do nothing with no switches */
	if (argc < 2)
		usage();

	track_buffer = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH);
	if(!track_buffer)
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
	track_inc = 2;

	reduce_sync = 3;
	fix_gcr = 1;
	align_disk = 0;
	auto_capacity_adjust = 1;
	verbose = 0;
	gap_match_length = 7;
	cap_min_ignore = 0;
	motor_speed = 300;
	rpm_real = 0;
	unformat_passes = 1;

	mode = MODE_WRITE_DISK;
	align = ALIGN_NONE;

	// cache our arguments for logfile generation
	strcpy(argcache, "");
	for (i = 0; i < argc; i++)
	{
		strcat(argcache, argv[i]);
		strcat(argcache," ");
	}

	/* default is to reduce sync */
	memset(reduce_map, REDUCE_SYNC, MAX_TRACKS_1541 + 1);

	while (--argc && (*(++argv)[0] == '-'))
		parseargs(argv);

	printf("\n");
	if (argc > 0)	strcpy(filename, argv[0]);

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

	if(mode == MODE_WRITE_DISK)
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
		case MODE_WRITE_RAW:
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

	/* read and remaster disk */
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
		printf("\nUnknown image type");

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

	/* turn on motor and measure speed */
	motor_on(fd);

	/* prepare fisk for writing */
	if(auto_capacity_adjust) adjust_target(fd);
	if(align_disk) init_aligned_disk(fd);

	if(mode == MODE_WRITE_RAW)
		master_disk_raw(fd, track_buffer, track_density, track_length);
	else
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
	     " -s[n]: Manual track skew (in ms)\n"
	     " -t: Enable timer-based track alignment\n"
	     " -g: Enable gap reduction\n"
	     " -0: Enable bad GCR run reduction\n"
	     " -r: Disable automatic sync reduction\n"
	     " -c: Disable automatic capacity adjustment\n"
	     " -f: Disable automatic bad GCR simulation\n"
	     " -ff: Enable more aggressive bad GCR simulation\n"
	     " -u: Unformat disk. (writes all 0 bits to surface)\n"
	     " -v: Verbose (output more detailed track data)\n"
	     " -G: Manual gap match length\n"
	     );
	exit(1);
}

