/*
    NIBCONV - part of the NIBTOOLS package for 1541/1571 disk image nibbling
	by Peter Rittwage <peter(at)rittwage(dot)com>
    based on code from MNIB, by Dr. Markus Brenner
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
BYTE track_density[MAX_HALFTRACKS_1541];
int track_length[MAX_HALFTRACKS_1541];

int reduce_syncs, reduce_weak, reduce_gaps;
int fixgcr, align, force_align;
int gap_match_length;

int ARCH_MAINDECL
main(int argc, char **argv)
{
	//FILE *fpin, *fpout;
	//	char *dotpos;
	char inname[256], outname[256];
	int skip_halftracks;
	int gap_match_length;

	fixgcr = 0;
	reduce_syncs = 0;
	reduce_weak = 0;
	reduce_gaps = 0;
	gap_match_length = 7;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;

	fprintf(stdout,
	  "\nnibconv - converts a CBM disk image from one format to another.\n"
	  "(C) Pete Rittwage, Dr. Markus Brenner, and friends.\n"
	  "Version " VERSION "\n\n");

	track_buffer = calloc(MAX_HALFTRACKS_1541, NIB_TRACK_LENGTH);
	if(!track_buffer)
	{
		printf("could not allocate memory for buffers.\n");
		exit(0);
	}

	while (--argc && (*(++argv)[0] == '-'))
	{
		switch ((*argv)[1])
		{
		case 'f':
			printf("* Fix weak GCR\n");
			fixgcr = 1;
			break;

		case 'r':
			printf("* Reduce syncs enabled\n");
			reduce_syncs = 1;
			break;

		case '0':
			printf("* Reduce weak GCR enabled\n");
			reduce_weak = 1;
			break;

		case 'g':
			printf("* Reduce gaps enabled\n");
			reduce_gaps = 1;
			break;

		case 'G':
			if (!(*argv)[2])
				usage();
			gap_match_length = atoi((char *) (&(*argv)[2]));
			printf("* Gap match length set to %d\n", gap_match_length);
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

		case 'h':
			printf("* Skipping halftracks\n");
			skip_halftracks = 1;
			break;

		default:
			usage();
			break;
		}
	}

	if (argc == 1)
	{
		strcpy(inname, argv[0]);
		strcpy(outname, inname);
	}
	else if (argc == 2)
	{
		strcpy(inname, argv[0]);
		strcpy(outname, argv[1]);
	}
	else
		usage();

	/* code logic here */


	return 0;
}

void
usage(void)
{
	fprintf(stderr, "usage: nibconv [options] <infile> <outfile>\n");
	exit(1);
}
