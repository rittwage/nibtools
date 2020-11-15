/*
 * Protection handlers and other low-level GCR modifications for NIBTOOLS
 * Copyright Pete Rittwage <peter(at)rittwage(dot)com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcr.h"
#include "prot.h"

extern int fattrack;

/* I don't like this kludge, but it is necessary to fix old files that lacked halftracks */
void search_fat_tracks(BYTE *track_buffer, BYTE *track_density, size_t *track_length)
{
	int track, numfats=0;
	size_t diff=0;
	char errorstring[0x1000];

	if(!fattrack) /* autodetect fat tracks */
	{
		//printf("Searching for fat tracks...\n");
		for (track=2; track<=MAX_HALFTRACKS_1541-1; track+=2)
		{
			if (track_length[track] > 0 && track_length[track+2] > 0 &&
				track_length[track] != 8192 && track_length[track+2] != 8192)
			{
				diff = compare_tracks(
				  track_buffer + (track * NIB_TRACK_LENGTH),
				  track_buffer + ((track+2) * NIB_TRACK_LENGTH),
				  track_length[track],
				  track_length[track+2], 1, errorstring);

				if(verbose>1) printf("%4.1f: %d\n",(float)track/2,diff);

				if (diff<2) /* 34 happens on empty formatted disks */
				{
					printf("Likely fat track found on T%d/%d (diff=%d)\n",track/2,(track/2)+1,(int)diff);

					memcpy(track_buffer + ((track+1) * NIB_TRACK_LENGTH),
						track_buffer + (track * NIB_TRACK_LENGTH),
						NIB_TRACK_LENGTH);

					track_length[track+1] = track_length[track];
					track_density[track+1] = track_density[track];

					if(!numfats)
						fattrack=track;
					else
					{
						printf("These are likely not fat tracks, just repeat data - Ignoring\n");
						//fattrack=0;
					}
					numfats++;
				}
			}
		}
	}
	else if(fattrack!=99) /* manually overridden */
	{
		printf("Handle FAT track on %d\n",fattrack/2);

		memcpy(track_buffer + ((fattrack+1) * NIB_TRACK_LENGTH),
			track_buffer + (fattrack * NIB_TRACK_LENGTH),
			NIB_TRACK_LENGTH);

		track_length[fattrack+1] = track_length[fattrack];
		track_density[fattrack+1] = track_density[fattrack];
	}
}

/* this routine tries to "fix" non-sync aligned images created from RAW Kryoflux stream files */
/* PROBLEM: This simple implementation can miss sync like 01111111 11111110 which is 14 bits and valid... */
/* PROBLEM: Many KF G64s begin the track in the middle of a sector, and is missed by this routine also */
size_t sync_align(BYTE *buffer, int length)
{
    int i, j;
    int bytes, bits;
	BYTE temp_buffer[NIB_TRACK_LENGTH];
	//BYTE *marker_pos;

	memset(temp_buffer, 0x00, NIB_TRACK_LENGTH);

    // first align buffer to a sync, shuffling
    i=0;
    while(!((buffer[i]==0xff)&&(buffer[i+1]&0x80)))
	{
    	i++;
    	if(i==length) break;
	}
	if(i<15) // header, skip that also
	{
		while(!((buffer[i]==0xff)&&(buffer[i+1]&0x80)))
		{
		    	i++;
		    	if(i==length) break;
		}
	}
	if(i==length) return 0;
	memcpy(temp_buffer, buffer+i, length-i);
	memcpy(temp_buffer+length-i, buffer, i);
    memcpy(buffer, temp_buffer, length);
    if(verbose>1) printf("{shuff:%d}", i);

    // shift buffer left to edge of sync marks
    for (i=0; i<length; i++)
    {
		if( ((buffer[i] == 0xff) && ((buffer[i+1] & 0x80) == 0x80) && (buffer[i+1] != 0xff)) ||
			((buffer[i] == 0x7f) && ((buffer[i+1] & 0xc0) == 0xc0) && (buffer[i+1] != 0xff)) )
		{
			i++;  //set first byte to shift
			bits=bytes=j=0;  //reset byte count

			// find next (normal) sync
			while(!((buffer[i+bytes] == 0xff) && (buffer[i+bytes+1] & 0x80)))
			{
				bytes++;
				if(i+bytes>length) break;
			}
			if(verbose>1) printf("(%d)", bytes);

			//shift left until MSB cleared
			while(buffer[i] & 0x80)
			{
				if(bits++>7)
				{
					if(verbose) printf("error shift too long!");
					break;
				}

				for(j=0; j<bytes; j++)
				{
					if(i+j+1>length-1) j=bytes;
					buffer[i+j] = (buffer[i+j] << 1) | ((buffer[i+j+1] & 0x80) >> 7);
				}
				//buffer[i+j] |= 0x1;
			}
			if(verbose>1) printf("[bits:%d]",bits);
		}
    }
    return 1;
}

void shift_buffer_left(BYTE *buffer, int length, int n)
{
    int i;
    BYTE tempbuf[NIB_TRACK_LENGTH + 1];
    BYTE carry;
    int carryshift = 8 - n;

    memcpy(tempbuf, buffer, length);
    tempbuf[length] = 0x00;

    // shift buffer left by n bits
    for (i = 0; i < length; i++)
    {
        carry = tempbuf[i+1];
        buffer[i] = (tempbuf[i] << n) | (carry >> carryshift);
    }
}

void shift_buffer_right(BYTE *buffer, int length, int n)
{
    int i;
    BYTE tempbuf[NIB_TRACK_LENGTH];
    BYTE carry;
    int carryshift = 8 - n;

    memcpy(tempbuf, buffer, length);

    // shift buffer right by n bits
    carry = 0;
    for (i = 0; i < length; i++)
    {
        buffer[i] = (tempbuf[i] >> n) | (carry << carryshift);
        carry = tempbuf[i];
    }
}

BYTE *
align_vmax(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end, *start_pos;
	int run;

	/* Try to find V-MAX track marker bytes	*/

	run = 0;
	pos = work_buffer;
	start_pos = work_buffer;
	buffer_end = work_buffer + tracklen + 1;

	while (pos < buffer_end)
	{
		// duplicator's markers
		if ( (*pos == 0x4b) || (*pos == 0x69) || (*pos == 0x49) || (*pos == 0x5a) || (*pos == 0xa5) )
		{
			if(!run) start_pos = pos;  // mark first byte
			if (run > 5) return (start_pos); // assume this is it
			run++;
		}
		else
			run = 0;

		pos++;
	}
	return (0);
}

BYTE *
align_vmax_new(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end, *key_temp, *key;
	int run, longest;

	run = 0;
	longest = 0;
	pos = work_buffer;
	buffer_end = work_buffer + tracklen + 1;
	key = key_temp = NULL;

	/* try to find longest good gcr run */
	while (pos < buffer_end - 2)
	{
		if ( (*pos == 0x4b) || (*pos == 0x69) || (*pos == 0x49) || (*pos == 0x5a) || (*pos == 0xa5) )
		{
			if(run > 2)
				key_temp = pos-run+1 ;
			run++;
		}
		else
		{
			if (run > longest)
			{
				key = key_temp;
				longest = run;
			}
			run = 0;
		}
		pos++;
	}
	return(key);
}

BYTE *
align_vmax_cw(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end, *start_pos;
	int run;

	run = 0;
	pos = work_buffer;
	start_pos = work_buffer;
	buffer_end = work_buffer + tracklen + 1;

	/* Cinemaware titles have a marker $64 $a5 $a5 $a5 */

	while (pos < buffer_end - 3)
	{
		// duplicator's markers
		if ( (*pos == 0x64) && (*(pos+1) == 0xa5) && (*(pos+2) == 0xa5) && (*(pos+3) == 0xa5) )
			return (pos); // assume this is it
		else
			pos++;
	}

	return (0);
}

BYTE *
align_pirateslayer(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end;
	BYTE backup_buffer[NIB_TRACK_LENGTH*2];
	int shift;

	/* backup since we are shifting */
	memcpy(backup_buffer, work_buffer, NIB_TRACK_LENGTH*2);

	for(shift=0; shift<8; shift++)
	{
		pos = work_buffer;
		buffer_end = work_buffer + tracklen + 1;

		/* try to find pslayer signature */
		while (pos < buffer_end-5)
		{
			if ( ((pos[0] == 0xd7) && (pos[1] == 0xd7) && (pos[2] == 0xeb) && (pos[3] == 0xcc) && (pos[4] == 0xad)) ||   /* version 1 and version 2 */
				/* it also looks for another byte pattern just after that: $55 $AE $9B $55 $AD $55 $CB $AE $6B $AB $AD $AF, but we only flag first one */
				((pos[0] == 0xeb) && (pos[1] == 0xd7) && (pos[2] == 0xaa) && (pos[3] == 0x55)) )  /* version 1 secondary check */
			{
				return pos - 5;  /* back up a little */
			}
			pos++;
		}
		printf(">>%d", shift+1);
		shift_buffer_right(work_buffer, tracklen, 1);
	}

	/* never found signature, restore original track data */
	memcpy(work_buffer, backup_buffer, NIB_TRACK_LENGTH*2);

	return NULL;
}

/* RL detector routine for nibtools.

Try to find longest good gcr run of RL-TH, check for RL/DOS format,
KS, RL-ver & RL-TV. Try to align tracks so they start with full RL-TH,
or end with RL-KS. Try to align unrecognized tracks to start with
sector 0 (or longest sync - can be customized by flag).

This detector may not work correctly on modified disk images.

Example how to activate the routine:
nibconv -pr "RLgameimage.nib" test1.g64

The RL version is determined on track 18. The RL TV standard is
determined on tracks 17 or 18, depending on RL version. Both info
is output together when >>track 18<< is detected, e.g. "<RL1-NTSC>".

When a RL-TH is detected some info is output to console, e.g.
"[RL:TH:21+1+164+1->166]" / "[DOS:TH:21+1+123+56]" or "[RL:DOS-Sec0]" /
"[DOS:DOS-Sec0]" :

"RL:", "DOS:" : RL or DOS formatted track.

"TH" means RL-TH was detected, "TH:21+1+164+1->166" means the RL-TH
starts with 21 sync bytes, 1 0x55 ID byte, 164 $7B bytes and (possibly)
1 off-byte. The length is 166 bytes (->Key). Sometimes a few $7B bytes
are replaced with $4B bytes on originals, this will be output as "THX"
instead of "TH".

"DOS-Sec0" means no RL-TH was detected and the track was aligned at
sync before sector 0.
*/


/* RLTV: Global value. Early RL versions have TV info on T17, not T18.
   RL TV standard is output only when RL version is recognized on T18,
   so we have to remember this value. */
static int RLTV = 0;


BYTE *
align_rl_special(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *pos2, *pos3, *pos4, *pos5, *pos6, *buffer_end, *key, *key_PreKS_Sync, *key_PreSec0_Sync, *key_KS;
	int longest, numGG, numFF, num55, num7B, num4B, numXX, Found_RL_TrackHeader, len_temp;
	int MaxNumFF, MaxNum55, MaxNum7B, MaxNum4B, MaxNumXX, Found_Max_RL_TrackHeader;
	int RL_Hdr_Found, RL_Sec_Found, NonRLStruct, RL_PreKS_Sync_Len, RL_Sec_Len;
	int DOS_Hdr_Found, DOS_Sec_Found, longest_PreSec0_Sync;
	int RLT17S0Identified, RLT18S15Identified, RLver, RLT18S18Identified, RLT18S17Identified;
	BYTE HandleSingleFFasDataByte, isSyncByte, DOSSecAlignRule, DOSSecAlign;
	int SecNum;

	/* Customizable settings: */
	DOSSecAlignRule = 1; /* Preferred DOS sector alignment: 1=Sec0, 2=longest sync before any DOS-Hdr */
	HandleSingleFFasDataByte = 1; /* 1=true, sometimes a single $FF is NOT a sync byte */
	/* End of customizable settings */

	/* Initializations: */
	longest = 0; /* length of longest RL-TH so far */
	longest_PreSec0_Sync = 0; /* longest sync length before DOS-Sec0/Hdr so far */
	pos = work_buffer; /* current buffer-pos */
	buffer_end = work_buffer + 2*tracklen; /* 2x instead of 1x !! Very cool idea from Pete !! */
	key = NULL; /* default alignment */
	numGG = RL_Hdr_Found = RL_Sec_Found = 0;
	numFF = num55 = num7B = num4B = numXX = Found_RL_TrackHeader = 0;
	MaxNumFF = MaxNum55 = MaxNum7B = MaxNum4B = MaxNumXX = Found_Max_RL_TrackHeader = 0;
	RL_Hdr_Found = RL_Sec_Found = NonRLStruct = RL_PreKS_Sync_Len = RL_Sec_Len = 0;
	DOS_Hdr_Found = DOS_Sec_Found = 0;
	RLT17S0Identified = RLT18S15Identified = RLver = RLT18S18Identified = RLT18S17Identified = 0;
	/* End of initializations */

	/* we have the track image two consecutive times, which simplifies everything. */

	/* try to find longest good gcr run of RL-TH, check for RL/DOS format,
	   KS, RL-ver & RL-TV. */

	while (pos < buffer_end)
	{
		/* Check if current byte is single non-sync 0xFF (happens!!) */

		isSyncByte = (*pos == 0xff);
		if (isSyncByte)
		{
			if (HandleSingleFFasDataByte)
			{
				/* Handle single $FFs as data bytes */
				/* It's sync if previous or next byte is also 0xFF */
				if (pos == work_buffer)
				{
					pos2 = buffer_end-1;
					pos3 = pos+1;
				}
				else if ( (work_buffer < pos) && (pos < buffer_end - 1) )
				{
					pos2 = pos-1;
					pos3 = pos+1;
				}
				else if ( (work_buffer < pos) && (pos == buffer_end - 1) )
				{
					pos2 = pos-1;
					pos3 = work_buffer;
				}
				else
				{
					pos2 = pos;
					pos3 = pos;
				}
				isSyncByte = (*pos2 == 0xff) | (*pos3 == 0xff); /* =TRUE if previous or next byte is also 0xFF */
			}

			if (isSyncByte)
				numGG++; /* Count sync bytes */
		}

		/* Now check for "RL $75+$6B sectors", "RL $6B KS", "DOS $52 Hdr",
		   "DOS $55 Sec", RL-T17S0 (RL-TV), RL-T18S15/17/18 (RL-ver/TV).
		   We have the track image two consecutive times, so we can skip many
		   "if"-cases. */

		if (!isSyncByte)
		{
			if ( (*pos == 0x75) && (0 < numGG) )
			{
				RL_Hdr_Found++; /* "RL $75 sector header" found */
				RLT17S0Identified = 0;
				RLT18S15Identified = 0;
				RLT18S17Identified = 0;
				RLT18S18Identified = 0;
			}
			else if ( (*pos == 0x6B) && (0 < numGG) )
			{
				if (++RL_Sec_Found == 1)
				{
					/* First "RL $6B data sector" found. This is ok, as track
					   gets aligned with first sync byte after this sector. */
					RL_Sec_Len++; /* Determine length of first found RL-KS */
					key_PreKS_Sync = pos - numGG; /* start of Pre-KS sync */
					key_KS = pos; /* start of KS */
				}
				RLT17S0Identified = 0;
				RLT18S15Identified = 0;
				RLT18S17Identified = 0;
				RLT18S18Identified = 0;
			}
			else if ( (*pos == 0x52) && (0 < numGG) )
			{
				DOS_Hdr_Found++; /* "DOS $52 sector header" found */
				NonRLStruct++;

				/* When track has no RL-TH align to DOS-Sec with longest preceding sync */
				pos2 = pos+2;
				pos3 = pos+3;
				SecNum = (((*pos2 & 0xf) << 6) | (*pos3 & 0xfc) >> 2); /* extract sector number in gcr format */
				//printf("<Sec#=%.3X>",SecNum);
				if (DOSSecAlignRule == 1)
					DOSSecAlign = ( (SecNum == 0x14a) && (numGG > longest_PreSec0_Sync) ); /* align: longest sync before DOS-Sec0 */
				else
					DOSSecAlign = (numGG > longest_PreSec0_Sync); /* align: longest sync before any DOS-Hdr */
				if (DOSSecAlign)
				{
					key_PreSec0_Sync = pos - numGG; /* set alignment */
					longest_PreSec0_Sync = numGG; /* remember sync length */
				}

				/* Identify T17-S0 & T18-S15/S17/S18 for RL-ver/TV check */
				if (pos < buffer_end - 4)
				{
					pos2 = pos+2;
					pos3 = pos+3;
					pos4 = pos+4;
					if ( ((*pos2 & 0xf) == 0x05) && (*pos3 == 0x29) && (*pos4 == 0x6B) ) RLT17S0Identified = 1;
					if ( ((*pos2 & 0xf) == 0x05) && (*pos3 == 0x55) && (*pos4 == 0x72) ) RLT18S15Identified = 1;
					if ( ((*pos2 & 0xf) == 0x05) && (*pos3 == 0xAD) && (*pos4 == 0x72) ) RLT18S17Identified = 1;
					if ( ((*pos2 & 0xf) == 0x05) && (*pos3 == 0xC9) && (*pos4 == 0x72) ) RLT18S18Identified = 1;
				}
				else
				{
					RLT17S0Identified = 0;
					RLT18S15Identified = 0;
					RLT18S17Identified = 0;
					RLT18S18Identified = 0;
				}
			}
			else if ( (*pos == 0x55) && (0 < numGG) )
			{
				DOS_Sec_Found++; /* "DOS $55 data sector found (RL1-FN has $5D-DOS-empty-sectors!) */
				NonRLStruct++;

				if (RLT17S0Identified)
				{
					if (pos < buffer_end - 194)
					{
						/* Identify RL-TV-standard */
						pos2 = pos+180;
						pos3 = pos+181;
						pos4 = pos+182;
						pos5 = pos+183;
						//printf("<%2X.%2X.%2X.%2X>",*pos2,*pos3,*pos4,*pos5);
						/* RL1-TV: */
						if ( (*pos2 == 0x54) && (*pos3 == 0xB4) && (*pos4 == 0xD5) && (*pos5 == 0x7B) ) RLTV = 1; /* 1=NTSC */
					}
				}

				if ( (RLT18S15Identified) && (RLver == 0) )
				{
					if (pos < buffer_end - 92)
					{
						/* Identify RL-ver */
						pos2 = pos+90;
						pos3 = pos+91;
						pos4 = pos+92;
						//printf("<%2X.%2X.%2X>",*pos2,*pos3,*pos4);
						if ( (*pos2 == 0xD2) && (*pos3 == 0xAA) && (*pos4 == 0xD7) ) RLver = 1;
						if ( (*pos2 == 0x7F) && (*pos3 == 0x5B) && (*pos4 == 0x36) ) RLver = 2;
						if ( (*pos2 == 0x72) && (*pos3 == 0x97) && (*pos4 == 0xE9) ) RLver = 3;
						if ( (*pos2 == 0xB5) && (*pos3 == 0xB3) && (*pos4 == 0x9D) ) RLver = 4;
						if ( (*pos2 == 0x92) && (*pos3 == 0x7A) && (*pos4 == 0xEF) ) RLver = 567; /* 5, 6 or 7 */
					}
				}

				if (RLT18S17Identified)
				{
					if (pos < buffer_end - 198)
					{
						/* Identify RL-TV-standard */
						pos2 = pos+195;
						pos3 = pos+196;
						pos4 = pos+197;
						pos5 = pos+198;
						pos6 = pos+199;
						//printf("<%2X.%2X.%2X.%2X.%2X>",*pos2,*pos3,*pos4,*pos5,*pos6);
						/* RL2-TV: */
						if ( (*pos2 == 0xF2) && (*pos3 == 0x65) && (*pos4 == 0xBF) && (*pos5 == 0x27) && (*pos6 == 0xDE) ) RLTV = 1; /* 1=NTSC */
						if ( (*pos2 == 0x92) && (*pos3 == 0xBD) && (*pos4 == 0x3B) && (*pos5 == 0x2A) && (*pos6 == 0xD6) ) RLTV = 1; /* 1=NTSC */
						if ( (*pos2 == 0xF2) && (*pos3 == 0x55) && (*pos4 == 0x2F) && (*pos5 == 0x25) && (*pos6 == 0x52) ) RLTV = 2; /* 1=PAL */
					}
				}

				if (RLT18S18Identified)
				{
					if (pos < buffer_end - 142)
					{
						/* Identify RL-ver */
						pos2 = pos+140;
						pos3 = pos+141;
						pos4 = pos+142;
						//printf("<%2X.%2X.%2X>",*pos2,*pos3,*pos4);
						if ( (*pos2 == 0x7C) && (*pos3 == 0x9A) && (*pos4 == 0xA7) ) RLver = 5;
						if ( (*pos2 == 0x9D) && (*pos3 == 0xB4) && (*pos4 == 0xE7) ) RLver = 6;
						if ( (*pos2 == 0xED) && (*pos3 == 0xDC) && (*pos4 == 0xF7) ) RLver = 7;
					}
					if (pos < buffer_end - 199)
					{
						/* Identify RL-TV-standard */
						pos2 = pos+196;
						pos3 = pos+197;
						pos4 = pos+198;
						pos5 = pos+199;
						//printf("<%2X.%2X.%2X.%2X>",*pos2,*pos3,*pos4,*pos5);
						if ( (*pos2 == 0xAF) && (*pos3 == 0x9A) && (*pos4 == 0xE6) && (*pos5 == 0xB5) ) RLTV = 1; /* RL6: 1=NTSC, RL7: 1=PAL!! */
						if ( (*pos2 == 0x9E) && (*pos3 == 0xAA) && (*pos4 == 0xE5) && (*pos5 == 0x73) ) RLTV = 2; /* RL6: 2=PAL */
						if ( (*pos2 == 0x96) && (*pos3 == 0xEA) && (*pos4 == 0xE5) && (*pos5 == 0xE9) ) RLTV = 3; /* RL7: 3=NTSC */
					}
				}
				RLT17S0Identified = 0;
				RLT18S15Identified = 0;
				RLT18S17Identified = 0;
				RLT18S18Identified = 0;
			}
			else if (0 < numGG)
			{
				NonRLStruct++; /* Unrecognized sector after sync found */
				RLT17S0Identified = 0;
				RLT18S15Identified = 0;
				RLT18S17Identified = 0;
				RLT18S18Identified = 0;
			}
			else if ( (RL_Sec_Found == 1) && (RL_Hdr_Found == 0) && (NonRLStruct == 0) )
			{
				RL_Sec_Len++; /* Determine length of first found RL-KS */
				RLT17S0Identified = 0;
				RLT18S15Identified = 0;
				RLT18S17Identified = 0;
				RLT18S18Identified = 0;
			}

			numGG = 0; /* we are not or no longer on sync */
		}

		/* From now on check for RL-TH (very simple check) */

		if ( (*pos == 0xff) && (numFF < 25) && (num55 == 0) )
		{
			numFF++; /* Count sync bytes */
			goto cont;
		}

		if ( (*pos == 0x55) && (13 < numFF) && (numFF < 25) && (num55 == 0) )
		{
			/* Unhandled case: Sometimes $7B extra sectors start with $24 instead of $55 (RL1:L) */
			num55++; /* $7B extra sectors start with 0x55 byte */
			goto cont;
		}

		if ( ( (*pos == 0x7B) || (*pos == 0x4B) ) && (13 < numFF) && (numFF < 25) && (num55 == 1) && (Found_RL_TrackHeader == 0) )
		{
			/* Sometimes $7B extra sectors contain randomly distributed $4B bytes (RL6:MP, RL2:D) */
			/* Unhandled case: Sometimes all $F6 instead of $7B (RL1:L) */
			if (*pos == 0x4B)
				num4B++; /* Count $4B extra sector bytes */
			num7B++; /* Count $7B extra sector bytes */
			goto cont;
		}

		if ( (13 < numFF) && (numFF < 25) && (num55 == 1) && (60 <= num7B) && (num7B <= 300) )
		{
			/* RL-TH found if:
			   14-24 0xFF sync bytes + 0x55 data byte + 60-300 0x7B/0x4B bytes */
			Found_RL_TrackHeader = 1;

			if (*pos != 0xff)
			{
				numXX++; /* Count non-$7B/$4B off-bytes after $7B extra sector */
				goto cont;
			}
			else
			{
				// Find longest RL-TH
				len_temp = numFF + num55 + num7B + numXX;
				if (len_temp > longest)
				{
					Found_Max_RL_TrackHeader = 1;
					key = pos - len_temp;
					longest = len_temp;
					MaxNumFF = numFF;
					MaxNum55 = num55;
					MaxNum7B = num7B;
					MaxNum4B = num4B;
					MaxNumXX = numXX;
				}
			}
		}

		numFF = num55 = num7B = num4B = numXX = Found_RL_TrackHeader = 0;
cont:
		pos++;
	}

	/* Print cool info about detected RL structures */

	if ( (RL_Hdr_Found > 0) && ( (RL_Sec_Found > 0) || (DOS_Sec_Found > 0) ) )
	{
		/* RL track with $75 sector headers and $6B/$55 data sectors */
		printf("[RL");
		if (Found_Max_RL_TrackHeader == 1)
		{
			if (MaxNum4B > 0) /* reveal $7B extra sectors that contain randomly distributed $4B */
				printf(":THX:%d+%d+%d{%d}+%d->%d]", MaxNumFF, MaxNum55, MaxNum7B, MaxNum4B, MaxNumXX, MaxNum55+MaxNum7B+MaxNumXX);
			else
				printf(":TH:%d+%d+%d+%d->%d]", MaxNumFF, MaxNum55, MaxNum7B, MaxNumXX, MaxNum55+MaxNum7B+MaxNumXX);
		}
		else
		{
			/* No TH found */
			if (longest_PreSec0_Sync > 0)
			{
				if (DOSSecAlignRule == 1)
					printf(":DOS-Sec0]"); /* align to DOS-Sec0 with longest preceding sync */
				else
					printf(":DOS-MaxSync]");
				key = key_PreSec0_Sync; /* align to DOS-Hdr with longest preceding sync */
			}
			else
				printf("]"); /* not even DOS sector found */
		}
	}
	else if ( (DOS_Hdr_Found > 0) && (DOS_Sec_Found > 0) )
	{
		/* DOS track with $55/$52 IDs, no $75 IDs */
		printf("[DOS");
		if (Found_Max_RL_TrackHeader == 1)
		{
			if (MaxNum4B > 0) /* reveal $7B extra sectors that contain randomly distributed $4B */
				printf(":THX:%d+%d+%d{%d}+%d]", MaxNumFF, MaxNum55, MaxNum7B, MaxNum4B, MaxNumXX);
			else
				printf(":TH:%d+%d+%d+%d]", MaxNumFF, MaxNum55, MaxNum7B, MaxNumXX);
		}
		else
		{
			/* No TH found */
			if (longest_PreSec0_Sync > 0)
			{
				if (DOSSecAlignRule == 1)
					printf(":DOS-Sec0]"); /* align to DOS-Sec0 with longest preceding sync */
				else
					printf(":DOS-MaxSync]");
				key = key_PreSec0_Sync; /* align to DOS-Hdr with longest preceding sync */
			}
			else
				printf("]"); /* not even DOS sector found */
		}
	}
	else if ( (RL_Sec_Found > 0) && (RL_Hdr_Found == 0) && (NonRLStruct == 0) && (100 < RL_Sec_Len) && (RL_Sec_Len < 350) )
	{
		/* RL-KS found, place it at end of track buffer */
		printf("[RL-KS:%d]", RL_Sec_Len); /* KS in first half of double-track-buffer */
		key = key_KS + RL_Sec_Len; /* key --> first byte after RL-KS */
		if (key >= work_buffer + tracklen)
			key = key_PreKS_Sync; /* choose sync-start in first half of double-track-buffer */
	}
	else
		printf("[Unknown!]"); /* Unknown track format */

	if (RLver)
	{
		printf("<RL%d", RLver);
		if (RLver == 7)
		{
			/* TV is only printed when RL version is recognized */
			if (RLTV == 1)
				printf("-PAL> ");
			else if (RLTV == 3)
				printf("-NTSC> ");
			else
				printf("-TV?> ");
		}
		else
		{
			/* TV is only printed when RL version is recognized */
			if (RLTV == 1)
				printf("-NTSC> ");
			else if (RLTV == 2)
				printf("-PAL> ");
			else
				printf("-TV?> ");
		}
	}
	else
		printf(" ");

	return key;
}

// Line up the track cycle to the start of the longest gap mark
// this helps some custom protection tracks master properly
BYTE *
auto_gap(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end, *key_temp, *key;
	int run, longest;

	run = 0;
	longest = 0;
	pos = work_buffer;
	buffer_end = work_buffer + tracklen + 1;
	key = key_temp = NULL;

	/* try to find longest run of any one byte */
	while (pos < buffer_end - 2)
	{
		if (*pos == *(pos + 1))	// && (*pos != 0x00 ))
		{
			key_temp = pos + 2;
			run++;
		}
		else
		{
			if (run > longest)
			{
				key = key_temp;
				longest = run;
				//gapbyte = *pos;
			}
			run = 0;
		}
		pos++;
	}

	/* last 5 bytes of gap */
	// printf("gapbyte: %x, len: %d\n",gapbyte,longest);
	//if(key >= work_buffer + 5)
	//	return(key - 5);
	//else
	return key;
}

// The idea behind this is that bad GCR commonly occurs
// at the ends of tracks when they were mastered.
// we can line up the track cycle to this
// in lieu of no other hints
BYTE *
find_bad_gap(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end, *key_temp, *key;
	int run, longest;

	run = 0;
	longest = 0;
	pos = work_buffer;
	buffer_end = work_buffer + tracklen + 1;
	key = key_temp = NULL;

	/* try to find longest bad gcr run */
	while (pos < buffer_end)
	{
		if (is_bad_gcr(work_buffer, buffer_end - work_buffer,
		  pos - work_buffer))
		{
			// mark next GCR byte
			key_temp = pos + 1;
			run++;
		}
		else
		{
			if (run > longest)
			{
				key = key_temp;
				longest = run;
			}
			run = 0;
		}
		pos++;
	}

	/* first byte after bad run */
	return key;
}

// Line up the track cycle to the start of the longest sync mark
// this helps some custom protection tracks master properly
BYTE *
find_long_sync(BYTE * work_buffer, size_t tracklen)
{
	BYTE *pos, *buffer_end, *key_temp, *key;
	int run, longest;

	run = 0;
	longest = 0;
	pos = work_buffer;
	buffer_end = work_buffer + tracklen + 1;
	key = key_temp = NULL;

	/* try to find longest sync run */
	while (pos < buffer_end)
	{
		if (*pos == 0xff)
		{
			if (run == 0)
				key_temp = pos;

			run++;
		}
		else
		{
			if (run > longest)
			{
				key = key_temp;
				longest = run;
			}
			run = 0;
		}
		pos++;
	}

	/* first byte of longest sync run */
	return (key);
}

#include <assert.h>

void fix_first_gcr(BYTE *gcrdata, size_t length, size_t pos)
{
    // fix first bad byte in a row
    unsigned int lastbyte, mask, data;
    BYTE dstmask;

    lastbyte = (pos == 0) ? gcrdata[length-1] : gcrdata[pos-1];
    data = ((lastbyte & 0x03) << 8) | gcrdata[pos];

    dstmask = 0x80;
    for (mask = (7 << 7); mask >= 7; mask >>= 1)
    {
        if ((data & mask) == 0)
            break;
        else
            dstmask = (dstmask >> 1) | 0x80;
    }
    assert(mask >= 7);
    gcrdata[pos] &= dstmask;
}


void fix_last_gcr(BYTE *gcrdata, size_t length, size_t pos)
{
    // fix last bad byte in a row
    unsigned int lastbyte, mask, data;
    BYTE dstmask;

    lastbyte = (pos == 0) ? gcrdata[length-1] : gcrdata[pos-1];
    data = ((lastbyte & 0x03) << 8) | gcrdata[pos];

    dstmask = 0x00;
    for (mask = 7; mask <= (7 << 7); mask = mask << 1)
    {
        if ((data & mask) == 0)
            break;
        else
            dstmask = (dstmask << 1) | 0x01;
    }
    assert(mask <= (7 << 7));
    gcrdata[pos] &= dstmask;
}
