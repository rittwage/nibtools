/*
    NIBREPAIR - part of the NIBTOOLS package for 1541/1571 disk image nibbling
	by Peter Rittwage <peter(at)rittwage(dot)com>
    based on code from MNIB, by Dr. Markus Brenner

	This program will repair some errors in G64 images based on common GCR misreads.

	1) tri-bit error, in which 01110 is misinterpreted as 01000
	2) low frequency error, in which 10010 is misinterpreted as 11000

	When these errors cannot be found, we will just change the checksum, making a slightly corrupt image that
	will be innacurate, but may load instead of just failing with a disk error
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>

#include "mnibarch.h"
#include "gcr.h"
#include "nibtools.h"
#include "version.h"

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
int track_length[MAX_HALFTRACKS_1541 + 1];

int start_track, end_track, track_inc;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, align, force_align;
int gap_match_length;
int skip_halftracks;
int verbose = 0;

int ARCH_MAINDECL
main(int argc, char **argv)
{
	char inname[256], outname[256], *dotpos;

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	fix_gcr = 0;
	reduce_sync = 1;
	reduce_badgcr = 0;
	reduce_gap = 0;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;


	fprintf(stdout,
	  "\nnibrepair - converts a damaged NIB/NB2/G64 to a new 'repaired' G64 file.\n"
	  "(C) Pete Rittwage, Dr. Markus Brenner, and friends.\n"
	  "Version " VERSION "\n\n");

	if(!(track_buffer = calloc(MAX_HALFTRACKS_1541 + 1, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate memory for buffers.\n");
		exit(0);
	}

	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{
		case 'f':
			printf("* Fix bad GCR\n");
			fix_gcr = 1;
			break;

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

		case 'G':
			if (!(*argv)[2]) usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
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
			}
			else
				printf("Unknown protection handler\n");
			break;

		case 'a':
			// custom alignment handling
			printf("ARG: Custom alignment = ");
			if ((*argv)[2] == '0')
			{
				printf("sector 0\n");
				force_align = ALIGN_SEC0;
			}
			else if ((*argv)[2] == 'w')
			{
				printf("longest bad gcr run\n");
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

		case 'h':
			printf("* Skipping halftracks\n");
			skip_halftracks = 1;
			break;

		case 'v':
			printf("* Verbose mode (more detailed track info)\n");
			verbose = 1;
			break;

		default:
			usage();
			break;
		}
	}

	if(argc < 1)	usage();
	strcpy(inname, argv[0]);

	strcpy(outname, inname);
	dotpos = strrchr(outname, '.');
	if (dotpos != NULL) *dotpos = '\0';
	strcat(outname, "_repaired.g64");

	printf("%s -> %s\n",inname, outname);

	/* convert */
	if (compare_extension(inname, "G64"))
	{
		if(!read_g64(inname, track_buffer, track_density, track_length))
			exit(0);
	}
	else if (compare_extension(inname, "NIB"))
	{
		if(!read_nib(inname, track_buffer, track_density, track_length, track_alignment))
			exit(0);
	}
	else if (compare_extension(inname, "NB2"))
	{
		if(!read_nb2(inname, track_buffer, track_density, track_length, track_alignment))
			exit(0);
	}
	else
	{
		printf("Unknown input file type\n");
		exit(0);
	}

	if(skip_halftracks) track_inc = 2;

	/* repair */
	{}

	write_g64(outname, track_buffer, track_density, track_length);
	return 0;
}

void
usage(void)
{
	fprintf(stderr,
	"usage: nibrepair [options] <infile>\n"
	"\nsupported file types for repair:\n"
	"NIB, NB2, G64\n"
	"\noptions:\n"
	" -a[x]: Force alternative track alignments (advanced users only)\n"
	" -p[x]: Custom protection handlers (advanced users only)\n"
     " -g: Enable gap reduction\n"
     " -0: Enable bad GCR run reduction\n"
     " -r: Enable automatic sync reduction\n"
     " -G: Manual gap match length\n");
	exit(1);
}
