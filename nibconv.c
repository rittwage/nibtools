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
#include "lz.h"


int _dowildcard = 1;

BYTE *compressed_buffer;
BYTE *file_buffer;
BYTE *track_buffer;
BYTE track_density[MAX_HALFTRACKS_1541 + 1];
BYTE track_alignment[MAX_HALFTRACKS_1541 + 1];
size_t track_length[MAX_HALFTRACKS_1541 + 1];
int file_buffer_size;
int start_track, end_track, track_inc;
int reduce_sync, reduce_badgcr, reduce_gap;
int fix_gcr, align, force_align;
int gap_match_length;
int cap_min_ignore;
int skip_halftracks;
int verbose;
int rpm_real;
int drive;
int auto_capacity_adjust;
int skew;
int align_disk;
int ihs;
int mode;
int unformat_passes;
int capacity_margin;
int align_delay;
BYTE fillbyte = 0x55;

int ARCH_MAINDECL
main(int argc, char **argv)
{
	char inname[256], outname[256];
	char *dotpos;
	FILE *fp;

	start_track = 1 * 2;
	end_track = 42 * 2;
	track_inc = 2;
	fix_gcr = 0;
	reduce_sync = 3;
	skip_halftracks = 0;
	align = ALIGN_NONE;
	force_align = ALIGN_NONE;
	gap_match_length = 7;
	cap_min_ignore = 0;
	verbose = 0;
	rpm_real = 0;

	/* default is to reduce sync */
	memset(reduce_map, REDUCE_SYNC, MAX_TRACKS_1541 + 1);
	memset(track_length, 0x2000, MAX_TRACKS_1541 + 1);

	fprintf(stdout,
	  "\nnibconv - converts a CBM disk image from one format to another.\n"
	  "(C) 2004-2010 Peter Rittwage\nC64 Preservation Project\nhttp://c64preservation.com\n"
	  "Version " VERSION "\n\n");


	if(!(file_buffer = calloc(MAX_HALFTRACKS_1541+2, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate buffer memory\n");
		exit(0);
	}

	if(!(track_buffer = calloc(MAX_HALFTRACKS_1541+1, NIB_TRACK_LENGTH)))
	{
		printf("could not allocate memory for buffers.\n");
		exit(0);
	}

	/* default is to reduce sync */
	memset(reduce_map, REDUCE_SYNC, MAX_TRACKS_1541+1);

	while (--argc && (*(++argv)[0] == '-'))
		parseargs(argv);

	if(argc < 1)	usage();

	strcpy(inname, argv[0]);

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

	printf("Converting %s -> %s\n\n",inname, outname);

	if( (fp=fopen(outname,"r")) )
	{
		fclose(fp);
		printf("File exists - Overwrite? (y/N)");
		if(getchar() != 'y') exit(0);
	}

	/* convert */
	if (compare_extension(inname, "D64"))
	{
		if(!(read_d64(inname, track_buffer, track_density, track_length))) exit(0);
	}
	else if (compare_extension(inname, "G64"))
	{
		if(!(read_g64(inname, track_buffer, track_density, track_length))) exit(0);
	}
	else if (compare_extension(inname, "NBZ"))
	{
		printf("Uncompressing NBZ...\n");
		if(!(compressed_buffer = calloc(MAX_HALFTRACKS_1541+2, NIB_TRACK_LENGTH)))
		{
			printf("could not allocate buffer memory\n");
			exit(0);
		}
		if(!(file_buffer_size = load_file(inname, compressed_buffer))) exit(0);
		if(!(file_buffer_size = LZ_Uncompress(compressed_buffer, file_buffer, file_buffer_size))) exit(0);
		if(!(read_nib(file_buffer, file_buffer_size, track_buffer, track_density, track_length))) exit(0);

		if( (compare_extension(outname, "G64")) || (compare_extension(outname, "D64")) )
			align_tracks(track_buffer, track_density, track_length, track_alignment);

		free(compressed_buffer);
	}
	else if (compare_extension(inname, "NIB"))
	{
		if(!(file_buffer_size = load_file(inname, file_buffer))) exit(0);
		if(!(read_nib(file_buffer, file_buffer_size, track_buffer, track_density, track_length))) exit(0);

		if( (compare_extension(outname, "G64")) || (compare_extension(outname, "D64")) )
			align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else if (compare_extension(inname, "NB2"))
	{
		if(!(read_nb2(inname, track_buffer, track_density, track_length))) exit(0);

		if( (compare_extension(outname, "G64")) || (compare_extension(outname, "D64")) )
			align_tracks(track_buffer, track_density, track_length, track_alignment);
	}
	else
	{
		printf("Unknown input file type\n");
		exit(0);
	}

	if (compare_extension(outname, "D64"))
	{
		if(!(write_d64(outname, track_buffer, track_density, track_length))) exit(0);
		printf("\nWARNING!\nConverting to D64 is a lossy conversion.\n");
		printf("All individual sector header and gap information is lost.\n");
		printf("It is suggested you use the G64 format for most disks.\n");
	}
	else if (compare_extension(outname, "G64"))
	{
		if(skip_halftracks) track_inc = 2;
		if(!(write_g64(outname, track_buffer, track_density, track_length))) exit(0);

		if (compare_extension(inname, "D64"))
		{
			printf("\nWARNING!\nConverting from D64/G64 to G64 is not normally useful.\n");
			printf("No individual sector header or gap information is stored in a D64 image,\n");
			printf("so it has to be recontructed to make this conversion.  If the program you are\n");
			printf("trying to use needs this information (such as for protection),\nit may still fail.\n");
		}
	}
	else if (compare_extension(outname, "NBZ"))
	{
		if( (compare_extension(inname, "D64")) ||
			(compare_extension(inname, "G64")) ||
			(compare_extension(inname, "NBZ")))
		{
			printf("Output format makes no sense from this input file.\n");
			exit(0);
		}
		if(!(compressed_buffer = calloc(MAX_HALFTRACKS_1541+2, NIB_TRACK_LENGTH)))
		{
			printf("could not allocate buffer memory\n");
			exit(0);
		}
		if(skip_halftracks) track_inc = 2;
		if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) exit(0);
		if(!(file_buffer_size = LZ_CompressFast(file_buffer, compressed_buffer, file_buffer_size))) exit(0);
		if(!(save_file(outname, compressed_buffer, file_buffer_size))) exit(0);
		free(compressed_buffer);
	}
	else if (compare_extension(outname, "NIB"))
	{
		if( (compare_extension(inname, "D64")) ||
			(compare_extension(inname, "G64")) ||
			(compare_extension(inname, "NIB")))
		{
			printf("Output format makes no sense from this input file.\n");
			exit(0);
		}
		if(skip_halftracks) track_inc = 2;
		if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) exit(0);
		if(!(save_file(outname, file_buffer, file_buffer_size))) exit(0);
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
	 " -f[x]: Enable more aggressive bad GCR simulation\n"
	 " -3: Compress track data to real 300rpm capacity\n"
     " -G: Manual gap match length\n");
	exit(1);
}
