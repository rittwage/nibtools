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

int _dowildcard = 1;

BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
BYTE fillbyte;
int track_length[MAX_HALFTRACKS_1541 + 1];
int start_track, end_track, track_inc;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, align, force_align;
int gap_match_length;
int cap_min_ignore;
int skip_halftracks;
int verbose;


int ARCH_MAINDECL
main(int argc, char **argv)
{
	char inname[256], outname[256], command[256], pathname[256];
	char *dotpos, *pathpos;
	int iszip = 0;
	int retval = 1;

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	fix_gcr = 1;
	reduce_sync = 1;
	reduce_badgcr = 0;
	reduce_gap = 0;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;
	cap_min_ignore = 0;
	verbose = 0;
	fillbyte = 0x55;

	fprintf(stdout,
	  "\nnibconv - converts a CBM disk image from one format to another.\n"
	  "(C) C64 Preservation Project\nhttp://c64preservation.com\n"
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
			if ((*argv)[2] == 'f')
			{
				fix_gcr = 2;
				printf("* Enabled more agressive bad GCR reproduction\n");
			}
			else
			{
				fix_gcr = 0;
				printf("* Disabled bad GCR bit reproduction\n");
			}

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
				fix_gcr = 0;
				force_align = ALIGN_AUTOGAP;
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
				force_align = ALIGN_SEC0;
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

		case 'm':
			printf("* Minimum capacity ignore on\n");
			cap_min_ignore = 1;
			break;

		case 'b':
			// custom fillbyte
			printf("* Custom fillbyte: ");
			if ((*argv)[2] == '0')
			{
				printf("0x00\n");
				fillbyte = 0x00;
			}
			if ((*argv)[2] == '5')
			{
				printf("0x55\n");
				fillbyte = 0x55;
			}
			if ((*argv)[2] == 'f')
			{
				printf("0xFF\n");
				fillbyte = 0xFF;
			}
			break;

		default:
			usage();
			break;
		}
	}

	if(argc < 1)	usage();

	strcpy(inname, argv[0]);

	/* unzip image if possible */
	if (compare_extension(inname, "ZIP"))
	{
		printf("Unzipping image...\n");
		dotpos = strrchr(inname, '.');
		if (dotpos != NULL) *dotpos = '\0';
		strcpy(pathname, inname);

		/* try to detect pathname */
		pathpos = strrchr(pathname, '\\');
		if (pathpos != NULL)
			*pathpos = '\0';
		else //*nix
		{
			pathpos = strrchr(pathname, '/');
			if (pathpos != NULL)
				*pathpos = '\0';
		}

		sprintf(command, "unzip %s.zip -d %s", inname, pathname);
		system(command);
		iszip++;
	}

	if(argc < 2)
	{
		strcpy(outname, inname);
		dotpos = strrchr(outname, '.');
		if (dotpos != NULL) *dotpos = '\0';

		 if(compare_extension(inname, "G64"))
			strcat(outname, ".d64");
		else
			strcat(outname, ".g64");
	}
	else
		strcpy(outname, argv[1]);

	printf("%s -> %s\n",inname, outname);

	/* convert */
	if (compare_extension(inname, "D64"))
		retval = read_d64(inname, track_buffer, track_density, track_length);
	else if (compare_extension(inname, "G64"))
		retval = read_g64(inname, track_buffer, track_density, track_length);
	else if (compare_extension(inname, "NIB"))
		retval = read_nib(inname, track_buffer, track_density, track_length, track_alignment);
	else if (compare_extension(inname, "NB2"))
		retval = read_nb2(inname, track_buffer, track_density, track_length, track_alignment);
	else
	{
		printf("Unknown input file type\n");
		retval = 0;
	}

	if(iszip)
	{
			unlink(inname);
			printf("Temporary file deleted.\n");
	}

	if(!retval) exit(0);

	if (compare_extension(outname, "D64"))
	{
		write_d64(outname, track_buffer, track_density, track_length);
		printf("\nWARNING!\nConverting to D64 is a lossy conversion.\n");
		printf("All individual sector header and gap information is lost.\n");
		printf("It is suggested you use the G64 format for most disks.\n");
	}
	else if (compare_extension(outname, "G64"))
	{
		if(skip_halftracks) track_inc = 2;

		if ( (compare_extension(inname, "NIB")) || (compare_extension(inname, "NB2")) )
			align_tracks(track_buffer, track_density, track_length, track_alignment);

		write_g64(outname, track_buffer, track_density, track_length);

		if (compare_extension(inname, "D64"))
		{
			printf("\nWARNING!\nConverting from D64/G64 to G64 is not normally useful.\n");
			printf("No individual sector header or gap information is stored in a D64 image,\n");
			printf("so it has to be recontructed to make this conversion.  If the program you are\n");
			printf("trying to use needs this information (such as for protection),\nit may still fail.\n");
		}
	}
	else if (compare_extension(outname, "NIB"))
	{
		if(skip_halftracks) track_inc = 2;

		if ( !(compare_extension(inname, "NB2")) )
		{
			printf("Output to NIB format makes no sense from this input file.\n");
			exit(0);
		}
		write_nib(outname, track_buffer, track_density, track_length);
		exit(0);
	}
	else if (compare_extension(outname, "NB2"))
	{
		printf("Output to NB2 format makes no sense from this input file.\n");
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
	fprintf(stderr,
	"usage: nibconv [options] <infile>.ext1 <outfile>.ext2\n"
	"\nsupported file extensions for ext1:\n"
	"NIB, NB2, D64, G64\n"
	"\nsupported file extensions for ext2:\n"
	"D64, G64\n"
	"\noptions:\n"
	" -a[x]: Force alternative track alignments (advanced users only)\n"
	" -p[x]: Custom protection handlers (advanced users only)\n"
     " -g: Enable gap reduction\n"
     " -0: Enable bad GCR run reduction\n"
     " -r: Disable automatic sync reduction\n"
	 " -f: Disable automatic bad GCR simulation\n"
	 " -ff: Enable more aggressive bad GCR simulation\n"
     " -G: Manual gap match length\n");
	exit(1);
}
