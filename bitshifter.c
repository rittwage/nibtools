/*
	bitshifter.c
	Copyright 2011 Arnd Menge
	---
	contains routines used by nibtools to sync align bitshifted track data.

	NOTE: ALPHA VERSION.
*/

int  isTrackBitshifted(BYTE *track_start, int track_length);
int  align_bitshifted_kf_track(BYTE *track_start, int track_length, BYTE **aligned_track_start, int *aligned_track_length);
int  align_bitshifted_track(BYTE *track_start, int track_length, BYTE **aligned_track_start, int *aligned_track_length);
BYTE ShiftCopyXBitsFromPBtoQC(BYTE **p1, BYTE *p1bit, BYTE **p2, BYTE *p2bit, int NumDataBits, BYTE mode);
BYTE find_end_of_bitshifted_sync(BYTE **pt, BYTE *gcr_end);
BYTE find_bitshifted_sync(BYTE **pt, BYTE *gcr_end);
int  isImageAligned(BYTE *track_buffer);

#ifndef min
#define min(a,b)  (((a) < (b))? (a) : (b))
#endif

// Determine if a track is bitshifted (sectors not sync aligned).
//
// 'track_start' points to start of track data.
// 'track_length' is the number of track data bytes.
//
// Return value:
//   1 = at least one data sector is bitshifted.
//   0 = all data bytes (non-sync) are correctly aligned,
//       or no sync found.
int isTrackBitshifted(BYTE *track_start, int track_length)
{
	BYTE *pt, *track_end;
	int numSyncs = 0; // Number of found syncs.

	pt = track_start;                           // pt -> start of track data
	track_end = track_start + track_length - 1; // track_end -> last valid track data byte

	// Check if all syncs end on byte boundary. A sync is at least 10 bits long and
	// therefore cannot start on the last track byte.
	while (pt < track_end)
	{
		// Search for sync between pt and track_end.
		// Returns bit position 1-8 of sync start at (updated) pt if sync is found,
		// returns 0 if no sync found. Bit positions are numbered 1-8.
		find_bitshifted_sync(&pt, track_end);

		if (pt < track_end)
		{
			// sync found (there can't be a sync if pt==track_end)
			numSyncs++;

			// A sync is at least 10 bits long and therefore cannot end on the same
			// track byte.
			pt++;

			// Determine if number of final '1' sync bits in first byte where a '0'
			// bit occurs is 0, or 8 if track image ends with sync.
			if (find_end_of_bitshifted_sync(&pt, track_end)%8 != 0)
				return 1; // data sector is bitshifted
		}
	}

	// Return 0 if no sync found.
	if (numSyncs == 0) return 0;

	// All data bytes (non-sync) are correctly aligned.
	return 0;
}


// Align a track that is possibly bitshifted (sectors not sync aligned).
// Pad bits are inserted before syncs until last sync byte ends on byte
// boundary, hence the first byte of following data sector starts on the
// next byte.
// Returns aligned track starting with sync (if a sync is found).
//
// 'track_start': points to start of track data.
//    On entry: points to (unaligned) source track data.
//    On exit, if aligned_track_start==NULL: points to aligned track data.
//             Aligned track data is cut off after 'track_length' bytes.
//
// 'track_length': the number of valid track data bytes.
//
// 'aligned_track_start':
//    ==NULL on entry: no change on exit.
//    !=NULL on entry: points to aligned track data on exit, starting with sync (if a sync is found).
//
// 'aligned_track_length':
//    If ==NULL on entry: no change on exit.
//    If !=NULL && aligned_track_start!=NULL on entry: the number of valid track data bytes on exit.
//
// Return value:
//   1: sync found, track aligned.
//   0: sync not found, non-aligned track returned.
//  -1: empty track detected.
int align_bitshifted_kf_track(BYTE *track_start, int track_length, BYTE **aligned_track_start, int *aligned_track_length)
{
	BYTE *sourcedata, *src_end, *pt;
	int SSB;
	int res = 1; // Default return value.

	if ((track_start == NULL) || (track_length == 0))
	{
		printf("{nodata}");
		*aligned_track_start = track_start;
		*aligned_track_length = track_length;
		return -1; // empty track detected
	}

	// Have two copies of (bitshifted) source track data in memory.
	sourcedata = malloc(track_length*2);
	memcpy(sourcedata             , track_start, track_length);
	memcpy(sourcedata+track_length, track_start, track_length);

	pt = sourcedata; // Work pointer on source data
	src_end = sourcedata + track_length - 1; // Pointer -> last source byte

	// Get out of possible initial sync on track cycle.
	while ((*pt & 0xff) && (pt < src_end))
		(*pt)++;

	// Find first sync.
	//
	// Possible syncs on track cycle:
	//   1.1111.1111|track cycle|1000.0000
	//     0001.1111|track cycle|1111.1000
	//     0000.0001|track cycle|1111.1111.1  <-- use (src_end+2) for this one
	//     0000.0000|track cycle|1111.1111.11 <-- use (src_end+2) for this one
	//
	// Returns updated pt pointing to first sync start.
	// Returns SYNC START BIT (SSB) = bit position 1-8 of sync start at
	//   pt if sync is found, or 0 if no sync found.
	//
	// On return: pt<=src_end+1 (first byte of second track copy),
	//            because we skipped initial sync in while loop above.
	SSB = find_bitshifted_sync(&pt, src_end+2);

	//printf("\nsourcedata=0x%x | pt=0x%x.%d | src_end=0x%x | #%d\n", sourcedata, pt, SSB, src_end, track_length);

	// Return if no sync found (no alignment without sync).
	if (SSB == 0)
	{
		printf("{nosync}");
		*aligned_track_start = track_start;
		*aligned_track_length = track_length;
		res = 0; // sync not found, non-aligned track returned.
	}
	else
		res = align_bitshifted_track(pt, track_length, aligned_track_start, aligned_track_length);

	//BYTE *tmp = *aligned_track_start+*aligned_track_length-1;
	//printf("aligned_track_start=0x%x | end=0x%x | #%d\n", *aligned_track_start, tmp, *aligned_track_length);

	free(sourcedata);

	return res;
}

// Align a track that is possibly bitshifted (sectors not sync aligned).
// Pad bits are inserted before syncs until last sync byte ends on byte
// boundary, hence the first byte of following data sector starts on the
// next byte.
// Returns aligned track starting at original position (not necessarily at sync),
// but track data will be aligned only after first found sync.
//
// 'track_start': points to start of track data.
//    On entry: points to (unaligned) source track data.
//    On exit, if aligned_track_start==NULL: points to aligned track data.
//             Aligned track data is cut off after 'track_length' bytes.
//
// 'track_length': the number of valid track data bytes.
//
// 'aligned_track_start':
//    ==NULL on entry: no change on exit.
//    !=NULL on entry: points to aligned track data on exit, starting at original position.
//
// 'aligned_track_length':
//    If ==NULL on entry: no change on exit.
//    If !=NULL && aligned_track_start!=NULL on entry: the number of valid track data bytes on exit.
//
// Function always returns 1 (Everything ok).
int align_bitshifted_track(BYTE *track_start, int track_length, BYTE **aligned_track_start, int *aligned_track_length)
{
	BYTE *nibdata;
	BYTE *pt, *p1, *p2;
	BYTE *gcr_end, *gcr_end2, *sync_start, *sync_end;
	BYTE p1bit, p2bit, first_sync;
	size_t SSB, LSB;
	size_t NumDataBits, NumPadBits, NumSyncBits;

	// Allocate & init memory for target (sync aligned) track data.
	// Source is 'track_length' long (bitshifted track data).
	// Target will be longer as we insert '0' pad bits for sync
	// alignment: choose 'track_length'*2 to be safe.
	nibdata = malloc(track_length*2);
	memset(nibdata, 0, track_length*2);

	gcr_end  = track_start + track_length - 1; // Pointer -> last source byte
	gcr_end2 = nibdata + track_length - 1;     // Pointer -> last target byte

	pt = track_start; // Work pointer on source data
	p1 = track_start; // p1 -> source (unaligned track data)
	p2 = nibdata;     // p2 -> target (aligned track data)

	p1bit = 1; // Start copy on bit position 1 of first source byte (*p1)
	p2bit = 0; // No used bits so far in first target byte (*p2)

	first_sync = 1; // Flag for identifying first found sync.

	// Loop while (aligned) source track bytes available.
	while (p1 <= gcr_end)
	{
		// Find next sync: start at source pointer pt, search until last
		// valid track data byte.
		// Returns updated pt pointing to next sync start.
		// Returns SYNC START BIT (SSB) = bit position 1-8 of sync start at
		//   pt if sync is found, or 0 if no sync found.
		SSB = find_bitshifted_sync(&pt, gcr_end);

		// Sync is at least 10 bits long and therefore cannot start on last
		// valid track data byte.
		if (pt < gcr_end)
		{
			// Sync start found at pt: at least 10 bits long, not necessarily
			// starting on first bit.
			sync_start = pt;

			// Move work pointer as sync won't end on same source byte.
			pt++;

			// Find end of sync: start at source pointer pt, search until
			// last valid track data byte.
			// Returns updated pt pointing to first byte where a 0-bit occurs.
			//   (This is NOT necessarily the last byte containing sync bits!)
			//   Or, if pt==gcr_end, track image ends with sync.
			// Returns LSB = number of final 1-bits in first byte where a
			//   0-bit occurs (bit number 1-7 of last 1-bit, or 0 if byte
			//   starts with 0-bit).
			LSB = find_end_of_bitshifted_sync(&pt, gcr_end);

			// Remember first byte where a 0-bit occurs.
			sync_end = pt;

			// Determine number of data bits from current source pointer until sync start.
			// Current 'p1bit' value is the next data bit position 1-8 inside p1 source byte.
			// Current 'SSB'   value is the start bit position 1-8 inside sync start byte.
			if (p1 < sync_start)
			{
				// Current source pointer p1 points to a byte position before the sync start byte.
				//
				// Example: p1.p1bit ... sync_start.SSB
				// >>> (sync_start - p1 - 1) full data bytes between both pointers.
				// >>> (9-p1bit) data bits in data byte at p1 pointer.
				// >>> (SSB-1) data bits in first sync byte, may be 0.
				//
				// Hence number of data bits before sync start:
				NumDataBits = ((sync_start - p1 - 1)*8) + (SSB-1) + (9-p1bit);
			}
			else
			{
				// Current source pointer p1 points to sync start byte (p1==sync_start).
				//
				// Number of data bits before sync start
				//   = bit position of sync start - bit position of source byte
				NumDataBits = SSB - p1bit;
			}

			// Determine number of sync bits between sync_start.SSB and sync_end.LSB
			NumSyncBits = ((sync_end - sync_start - 1)*8 + LSB) + (9-SSB);

			// Determine number of required '0' pad bits to insert before sync start for sync
			// to end on byte alignment:
			// We have 'p2bit' bits in use at target pointer p2.
			// We have to copy NumDataBits before sync starts.
			// We have to copy NumSyncBits.
			// >>> We have to insert NumPadBits for the sum to be a multiple of 8 (the sync alignment!)
			NumPadBits = (8 - (p2bit + NumDataBits + NumSyncBits)%8)%8;

			// Generate verbose output if flagged.
			if (verbose > 1)
			{
				if (first_sync)
				{
					first_sync = 0;
					if (NumDataBits > 0)
					{
						if (NumDataBits%8 == 0) printf("0:%d ", NumDataBits/8);
						else printf("0:%d.%d ", NumDataBits/8, NumDataBits%8);
					}
					if (NumSyncBits%8 == 0) printf("%d:", NumSyncBits/8);
					else printf("%d.%d:", NumSyncBits/8, NumSyncBits%8);
				}
				else
				{
					if (NumDataBits%8 == 0) printf("%d ", NumDataBits/8);
					else printf("%d.%d ", NumDataBits/8, NumDataBits%8);
					if (NumSyncBits%8 == 0) printf("%d:", NumSyncBits/8);
					else printf("%d.%d:", NumSyncBits/8, NumSyncBits%8);
				}
			}
			// printf("SSB=%d LSB=%d #DataBits=%d(%d.%d) #SyncBits=%d #PadBits=%d \n",
			// SSB, LSB, NumDataBits, NumDataBits/8, NumDataBits%8, NumSyncBits, NumPadBits);

			// Bitshift and copy NumDataBits data bits (mode=99) from source position
			// p1.p1bit to target position p2.p2bit :
			// 'p1bit' is bit number 1-8 of next bit to be copied at pointer p1.
			// 'p2bit' is number of last written bit in target byte at pointer p2
			//         (1-7, 0 if no bit written so far at pointer p2).
			// Updated positions p1.p1bit and p2.p2bit are returned!
			ShiftCopyXBitsFromPBtoQC(&p1, &p1bit, &p2, &p2bit, NumDataBits, 99);

			// Insert NumPadBits '0' pad bits (mode=0) at target position p2.p2bit
			// (before sync start)
			// Updated position p2.p2bit is returned, p1.p1bit does not change.
			// NOTE: Too many zero pad bits may result in random bits enlarging sync.
			ShiftCopyXBitsFromPBtoQC(&p1, &p1bit, &p2, &p2bit, NumPadBits,   0);

			// Bitshift and copy NumSyncBits '1' sync bits (mode=1) from source position
			// p1.p1bit to target position p2.p2bit
			// Updated positions p1.p1bit and p2.p2bit are returned!
			ShiftCopyXBitsFromPBtoQC(&p1, &p1bit, &p2, &p2bit, NumSyncBits,  1);
		}
		else
		{
			// pt==gcr_end, no (more) sync found before track end.

			// Generate verbose output if flagged.
			if (verbose > 1)
			{
				if (p1 == track_start)
					printf("0:"); // no sync on track
				if ((8-LSB) != 0)
					printf("%d.%d\n", pt - sync_end, (8-LSB) );
				else
					printf("%d\n", pt - sync_end );
			}

			// Copy last source bytes to target.

			// Determine number of last data bits from current source pointer until track end.
			//
			// Current 'p1bit' value is the bit number 1-8 of the next bit to be copied inside
			// p1 source byte.
			// Current source pointer p1 points to a byte position before track end.
			//
			// Example: p1.p1bit ... gcr_end.0
			// >>> (gcr_end - p1 - 1) full data bytes between both pointers.
			// >>> (9-p1bit) data bits in data byte at p1 pointer.
			// >>> 8 data bits in last track byte.
			//
			// Hence number of data bits before end of track:
			NumDataBits = ((gcr_end - p1 - 1)*8) + 8 + (9-p1bit);

			// Bit shift and copy NumDataBits data bits (mode=99) before end of track
			// from source position p1.p1bit to target position p2.p2bit :
			// 'p1bit' is bit number 1-8 of next bit to be copied at pointer p1.
			// 'p2bit' is number of last written bit in target byte at pointer p2
			//         (1-7, 0 if no bit written so far at pointer p2).
			// Updated positions p1.p1bit and p2.p2bit are returned!
			ShiftCopyXBitsFromPBtoQC(&p1, &p1bit, &p2, &p2bit, NumDataBits, 99);

			// Generate verbose output if flagged.
			if (verbose > 2)
			{
				printf("P.B=0x%x.%d | gcr_end=0x%x | nibdata=0x%x | Q.C=0x%x.%d | #%d\n", p1, p1bit, gcr_end, nibdata, p2, p2bit, NumDataBits);
			}

			// Don't forget last bits of last byte (target memory was initialized with zeros by memset).
			if (p2bit != 0)
			{
				p2bit = 0;
				p2++;
			}
		}

	} // while (p2 <= gcr_end2)

	if (aligned_track_start == NULL) {
		// Copy sync aligned track over original track: nibdata -> track_start,
		// cut off after 'track_length'.
		memcpy(track_start, nibdata, track_length);
	} else {
		// Return length of sync aligned track, cut off 0-7 left over bits.
		*aligned_track_length = p2-nibdata;

		// Return complete sync aligned track.
		*aligned_track_start = malloc(*aligned_track_length);
		memcpy(*aligned_track_start, nibdata, *aligned_track_length);

		// Generate verbose output if flagged.
		if (verbose > 2)
		{
			printf(">>> 0x%x..0x%x..0x%x\n", nibdata             , *aligned_track_length, nibdata             +*aligned_track_length-1);
			printf(">>> 0x%x..0x%x..0x%x\n", *aligned_track_start, *aligned_track_length, *aligned_track_start+*aligned_track_length-1);
		}
	}

	// Free work memory.
	free(nibdata);

	// 1 = Everything ok.
	return 1;
}


// Copy NumDataBits bits from source to target.
//
// Source may be bitshifted track data (pointer *p, pointer b to bit number of next bit to be copied)
// or special byte depending on 'mode'.
// Target location specified by pointer *q and pointer c to number of last written bit in target byte
// (1-7, 0 if no bit written so far).
//
// Mode 0: Insert '0' bits (before sync).
// Mode 1: Insert '1' bits (sync).
// Mode 99: Copy bitshifted track data.
//
// Returns always 1 (Everything ok).
BYTE
ShiftCopyXBitsFromPBtoQC(BYTE **p, BYTE *b, BYTE **q, BYTE *c, int NumDataBits, BYTE mode)
{
	BYTE db, d;

	// Loop while bits are to be copied (return if target buffer is full, see below)
	while (NumDataBits > 0)
	{
		// If target Q.C byte is full move Q (pointer) to next byte and reset C (used bits)
		if (*c == 8)
		{
			*c = 0;
			(*q)++;
		}

		// Determine which bits to insert/copy.
		if (mode == 0) db = 0;         // Mode  0: Insert '0' bits (before sync)
		else if (mode == 1) db = 0xff; // Mode  1: Insert '1' bits (sync)
		else db = **p;                 // Mode 99: Copy (bitshifted) track data

		// Copy bits from 'db' to Q, but no more than 'db' has, and at most NumDataBits:
		// > Number of used bits in target byte Q.C = *c
		// > Number of free bits in target byte Q.C = (8-*c)
		// > 0 <= C <= 7 (see above)
		// > *b = Next bit position to be copied from 'db' (1 <= B <= 8)
		// > [ ((Q << C) & 0xff00) + new bits from db ] >> C
		**q = ( ( (int)((**q) >> (8-*c)) << 8) | (((int)db << (*b-1)) & 0xff) ) >> *c;

		// Determine number 'd' of copied bits (lowest value of following):
		// - At most (8-*c) free bits in Q were filled
		// - At most (9-*b) bits could be copied from db
		// - At most NumDataBits were left to be copied
		d = min(min(8-*c, 9-*b), NumDataBits);
		// Now: 1 <= d <= 8

		// Update source position P.B
		if (mode > 0)
		{
			*b += d; // Add number of copied bits: 1 <= B <= 9 (because of min(..,9-*b) above)
			*p += *b/9; // Move P only if B points to next byte's first bit (9)
			if (*b == 9) *b = 1; // Reset B if it points to next byte's first bit (P just updated)
		}

		// 'd' bits added/copied, update NumDataBits and C (bit counter)
		NumDataBits -= d;
		*c += d;

		// At most (8-*c) free bits in Q were filled (see above)
		// If target Q.C byte is full move Q (pointer) to next byte and reset C (used bits)
		if (*c == 8)
		{
			*c = 0;
			(*q)++;
		}
	}
	return 1;
}


// Returns number of final '1' sync bits in first byte where a '0' bit occurs.
// On return *pt points to first byte where a '0' bit occurs,
// or *pt==gcr_end when track image ends with sync.
BYTE
find_end_of_bitshifted_sync(BYTE **pt, BYTE *gcr_end)
{
    BYTE last_sync_bit = 0;

	// skip 0xff sync bytes
	while (*pt < gcr_end && **pt == 0xff) (*pt)++;

	if (*pt <= gcr_end)
	{
		if ( **pt == 0xff )               // 1111.1111
			last_sync_bit = 8;
		else if ( (**pt & 0xfe) == 0xfe ) // 1111.1110
			last_sync_bit = 7;
		else if ( (**pt & 0xfc) == 0xfc ) // 1111.1100
			last_sync_bit = 6;
		else if ( (**pt & 0xf8) == 0xf8 ) // 1111.1000
			last_sync_bit = 5;
		else if ( (**pt & 0xf0) == 0xf0 ) // 1111.0000
			last_sync_bit = 4;
		else if ( (**pt & 0xe0) == 0xe0 ) // 1110.0000
			last_sync_bit = 3;
		else if ( (**pt & 0xc0) == 0xc0 ) // 1100.0000
			last_sync_bit = 2;
		else if ( (**pt & 0x80) == 0x80 ) // 1000.0000
			last_sync_bit = 1;
	}

	return last_sync_bit;
}


// Search for sync between *pt and gcr_end.
// Returns bit position 1-8 of sync start at (updated) *pt if sync is found,
// returns 0 if no sync found. Bit positions are numbered 1-8.
BYTE
find_bitshifted_sync(BYTE **pt, BYTE *gcr_end)
{
	/* Possible bitshifted sync starts are:

	   11111111.11
	    1111111.111
	     111111.1111
	      11111.11111
	       1111.111111
	        111.1111111
	         11.11111111
	          1.11111111.1
	*/

	BYTE sync_start_bit = 0;

	while ( ((*pt) < gcr_end) && (!sync_start_bit) )
	{
		// 11111111.11
		if ( (*pt)[0] == 0xff && ((*pt)[1] & 0xc0) == 0xc0 )
			sync_start_bit = 1; // bits numbered 1-8

		// 1111111.111
		else if ( ((*pt)[0] & 0x7f) == 0x7f && ((*pt)[1] & 0xe0) == 0xe0 )
			sync_start_bit = 2; // bits numbered 1-8

		// 111111.1111
		else if ( ((*pt)[0] & 0x3f) == 0x3f && ((*pt)[1] & 0xf0) == 0xf0 )
			sync_start_bit = 3; // bits numbered 1-8

		// 11111.11111
		else if ( ((*pt)[0] & 0x1f) == 0x1f && ((*pt)[1] & 0xf8) == 0xf8 )
			sync_start_bit = 4; // bits numbered 1-8

		// 1111.111111
		else if ( ((*pt)[0] & 0x0f) == 0x0f && ((*pt)[1] & 0xfc) == 0xfc )
			sync_start_bit = 5; // bits numbered 1-8

		// 111.1111111
		else if ( ((*pt)[0] & 0x07) == 0x07 && ((*pt)[1] & 0xfe) == 0xfe )
			sync_start_bit = 6; // bits numbered 1-8

		// 11.11111111
		else if ( ((*pt)[0] & 0x03) == 0x03 && (*pt)[1] == 0xff )
			sync_start_bit = 7; // bits numbered 1-8

		// 1.11111111.1
		else if ( ((*pt)+1) < gcr_end )
			if ( ((*pt)[0] & 0x01) == 0x01
				&& (*pt)[1] == 0xff
				&& ((*pt)[2] & 0x80) == 0x80 )
					sync_start_bit = 8; // bits numbered 1-8

		if (!sync_start_bit) (*pt)++;
	}

	return sync_start_bit;
}


// Check sector alignment of a whole disk image.
// Disk image starts at 'track_buffer' pointer.
// Each track has to be NIB_TRACK_LENGTH bytes long.
int isImageAligned(BYTE *track_buffer)
{
	int track;

	BYTE *gcr_start;
	int imgres;

	printf("\nChecking sector alignment...\n");

	imgres = 1;

	for (track = start_track; track <= end_track; track += track_inc)
	{
		printf("%4.1f: ",(float) track / 2);

		gcr_start = track_buffer + (track * NIB_TRACK_LENGTH);

		if (!isTrackBitshifted(gcr_start, NIB_TRACK_LENGTH))
			printf("aligned\n");
		else
		{
			printf("bitshifted\n");
			imgres = 0;
		}
	}

	return imgres;
}
