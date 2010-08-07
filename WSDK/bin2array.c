/****************************************************************************

   PROGRAM: Bin2Array.c

     Version 1.00

     Copyright 2009 Arnd Menge <arnd(at)jonnz(dot)de>

   PURPOSE:

     Converts a binary file to a C array and places it in a file
     that can be included in a C project source.

   VERSION HISTORY:

     v1.00 -- initial version.

   LEGAL NOTICE:

     THIS FILE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND
     AND/OR FITNESS FOR A PARTICULAR PURPOSE. USE AT YOUR OWN RISK.

****************************************************************************/

#include <stdio.h>

int main(int argc, char *argv[])
{
    FILE *fpin, *fpout;
    unsigned char buf[1];
    size_t numread;
    int i = 1;

    printf( "Bin2Array v1.00 - Binary to C array conversion\n" );
    printf( "Copyright 2009 Arnd Menge <arnd(at)jonnz(dot)de>\n\n" );

    if (argc == 3)
    {
        if ( (fpin = fopen( argv[1], "rb" )) != NULL )
        {
            if ( (fpout = fopen( argv[2], "w" )) != NULL )
            {
                printf( "Converting: %s -> %s\n", argv[1], argv[2] );

                if ((numread = fread(buf, 1, 1, fpin)) != 0)
                    while (numread)
                    {
                        if (i % 8 == 1) fprintf(fpout," ");
                        fprintf(fpout,"0x%.2x",buf[0]);
                        if ((numread = fread(buf, 1, 1, fpin)) != 0)
							fprintf(fpout,",");
                        if (i % 8 == 0) fprintf(fpout,"\n");
						i++;
                    }
                fclose(fpout);
            }
            else printf("Cannot create file: %s", argv[2]);
            fclose(fpin);
        }
        else printf("File not found: %s", argv[1]);
    }
    else {
    	printf("Usage:   bin2array <input binary file> <output c array file>\n");
    	printf("Example: bin2array myfile.bin myarray.inc\n");
    }
}
