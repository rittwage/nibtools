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

int start_track, end_track, track_inc;
int reduce_syncs, reduce_weak, reduce_gaps;
int fix_gcr, align, force_align;
int gap_match_length;
int skip_halftracks;

int ARCH_MAINDECL
main(int argc, char **argv)
{
	char inname[256], outname[256];

	start_track = 1 * 2;
	end_track = 41 * 2;
	track_inc = 2;
	fix_gcr = 0;
	reduce_syncs = 1;
	reduce_weak = 0;
	reduce_gaps = 0;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;


	fprintf(stdout,
	  "\nnibconv - converts a CBM disk image from one format to another.\n"
	  "(C) Pete Rittwage, Dr. Markus Brenner, and friends.\n"
	  "Version " VERSION "\n\n");

	if(!(track_buffer = calloc(MAX_HALFTRACKS_1541, NIB_TRACK_LENGTH)))
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
			fix_gcr = 1;
			break;

		case 'r':
			printf("* Reduce syncs disabled\n");
			reduce_syncs = 0;
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

	if(argc < 2) usage();
	strcpy(inname, argv[0]);
	strcpy(outname, argv[1]);

	/* convert */
	if (compare_extension(inname, "D64"))
	{
		if(!read_d64(inname, track_buffer, track_density, track_length))
			exit(0);
	}
	else if (compare_extension(inname, "G64"))
	{
		if(!read_g64(inname, track_buffer, track_density, track_length))
			exit(0);
	}
	else if (compare_extension(inname, "NIB"))
	{
		if(!read_nib(inname, track_buffer, track_density, track_length))
			exit(0);
	}
	else if (compare_extension(inname, "NB2"))
	{
		if(!read_nb2(inname, track_buffer, track_density, track_length))
			exit(0);
	}
	else
	{
		printf("Unknown input file type\n");
		exit(0);
	}

	if (compare_extension(outname, "D64"))
	{
		write_d64(outname, track_buffer, track_density, track_length, 0);
		printf("\nWARNING!\nConverting to D64 is a lossy conversion.\n");
		printf("All individual sector header and gap information is lost.\n");
		printf("It is suggested you use the G64 format for most disks.\n");
	}
	else if (compare_extension(outname, "G64"))
	{
		if(skip_halftracks) track_inc = 2;

		write_g64(outname, track_buffer, track_density, track_length, 0);
		if (compare_extension(inname, "G64"))
		{
			printf("\nWARNING!\nConverting from D64 to G64 is not entirely useful.\n");
			printf("All individual sector header and gap information is not stored in a D64 image,\n");
			printf("so it has to be recontructed to make this conversion.  If the program you are\n");
			printf("trying to use needs this information (such as for protection),\nit may still fail.\n");
		}
	}
	else if (compare_extension(outname, "NIB"))
	{
		printf("Output to NIB format makes no sense\n");
		exit(0);
	}
	else if (compare_extension(outname, "NB2"))
	{
		printf("Output to NB2 format makes no sense\n");
		exit(0);
	}
	else
	{
		printf("Unknown output file type\n");
		exit(0);
	}
	return 0;
}

void
usage(void)
{
	fprintf(stderr, "usage: nibconv [options] <infile> <outfile>\n");
	exit(1);
}
