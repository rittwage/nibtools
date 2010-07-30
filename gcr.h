/*
    gcr.h - Group Code Recording helper functions

    (C) 2001-05 Markus Brenner <markus(at)brenner(dot)de>
    	and Pete Rittwage <peter(at)rittwage(dot)com>
        based on code by Andreas Boose

    V 0.33   improved sector extraction, added find_track_cycle() function
    V 0.34   added MAX_SYNC_OFFSET constant, approximated to 800 GCR bytes
    V 0.35   modified find_track_cycle() interface
    V 0.36   added bad GCR code detection
    V 0.36a  added find_sector_gap(), find_sector0(), extract_GCR_track()
    V 0.36d  Untold number of additions and consequent bugfixes. (pjr)

*/

#ifdef DJGPP
#define size_t unsigned int
#endif


#define BYTE unsigned char
#define DWORD unsigned int
#define MAX_TRACKS_1541 42 /* tracks are referenced 1-42 instead of 0-41 */
#define MAX_TRACKS_1571 (MAX_TRACKS_1541 * 2)
#define MAX_HALFTRACKS_1541 (MAX_TRACKS_1541 * 2)
#define MAX_HALFTRACKS_1571 (MAX_TRACKS_1571 * 2)

/* D64 constants */
#define BLOCKSONDISK 683
#define BLOCKSEXTRA 85
#define MAXBLOCKSONDISK (BLOCKSONDISK+BLOCKSEXTRA)
#define MAX_TRACK_D64 40

#define SYNC_LENGTH 	5
#define HEADER_LENGTH 	10
#define HEADER_GAP_LENGTH 	9  // this must be 9 or 1541 will corrupt the sector if written
#define DATA_LENGTH 	325 			// 65 * 5
#define SECTOR_GAP_LENGTH 		7  // this varies by drive motor speed and sector from 4-19

#define SECTOR_SIZE ((SYNC_LENGTH) + (HEADER_LENGTH) + (HEADER_GAP_LENGTH) + \
											(SYNC_LENGTH) + (DATA_LENGTH) + (SECTOR_GAP_LENGTH))

/* G64 constants (only needed for current VICE support */
#define G64_TRACK_MAXLEN 7928
#define G64_TRACK_LENGTH (G64_TRACK_MAXLEN+2)

/* NIB format constants */
#define NIB_TRACK_LENGTH 0x2000
#define NIB_HEADER_SIZE 0xFF

/*
    number of GCR bytes until NO SYNC error
    timer counts down from $d000 to $8000 (20480 cycles)
    until timeout when waiting for a SYNC signal
    This is approx. 20.48 ms, which is approx 1/10th disk revolution
    8000 GCR bytes / 10 = 800 bytes
*/
//#define MAX_SYNC_OFFSET 800
/* this was too small for Lode Runner original (805), so increase to 820 */
//#define MAX_SYNC_OFFSET 820
#define MAX_SYNC_OFFSET 0x1500

#define SIGNIFICANT_GAPLEN_DIFF 0x20
#define GCR_BLOCK_HEADER_LEN 24
#define GCR_BLOCK_DATA_LEN   337
#define GCR_BLOCK_LEN (GCR_BLOCK_HEADER_LEN + GCR_BLOCK_DATA_LEN)

/* To calculate the bytes per rotation:

            			4,000,000 * 60
   b/minute = ------------------------------------------------ = x  bytes/minute
             			speed_zone_divisor * 8bits

4,000,000 is the base clock frequency divided by 4.
8 is the number of bits per byte.
60 gets us to a minute of data, which we can then divide by RPM to
get our numbers.

speed zone divisors are 13, 14, 15, 16 for densities 3, 2, 1, 0 respectively
*/

#define DENSITY3 2307692.308 // bytes per minute
#define DENSITY2 2142857.143
#define DENSITY1 2000000.000
#define DENSITY0 1875000.000

/* Some disks have much less or more data than we normally expect to be able to write at a given density.
	It's like short tracks, but it's a mastering issue not a protection.
    This keeps us from getting errors in the track cycle detection */
#define CAP_ALLOWANCE 0x100

/* minimum amount of good sequential GCR for formatted track */
#define GCR_MIN_FORMATTED 16
//#define GCR_MIN_FORMATTED 64	// chessmaster track 29

/* Disk Controller error codes */
#define SECTOR_OK           0x01
#define HEADER_NOT_FOUND    0x02
#define SYNC_NOT_FOUND      0x03
#define DATA_NOT_FOUND      0x04
#define BAD_DATA_CHECKSUM   0x05
#define BAD_GCR_CODE        0x06
#define VERIFY_ERROR        0x07
#define WRITE_PROTECTED     0x08
#define BAD_HEADER_CHECKSUM 0x09
#define ID_MISMATCH         0x0b
#define DISK_NOT_INSERTED   0x0f

#define BM_MATCH       0x10
#define BM_NO_CYCLE	   0x20
#define BM_NO_SYNC     0x40
#define BM_FF_TRACK    0x80

#define ALIGN_NONE					0
#define ALIGN_GAP					1
#define ALIGN_SEC0					2
#define ALIGN_LONGSYNC		3
#define ALIGN_BADGCR			4
#define ALIGN_VMAX					5
#define ALIGN_AUTOGAP			6
#define ALIGN_VMAX_CW		7
#define ALIGN_RAW 8

#define REDUCE_NONE   0x0
#define REDUCE_SYNC	0x1
#define REDUCE_GAP		0x2
#define REDUCE_BAD		0x4

/* global variables */
extern BYTE sector_map[];
extern BYTE speed_map[];
extern BYTE align_map[];
extern BYTE reduce_map[];
extern size_t capacity[];
extern size_t capacity_min[];
extern size_t capacity_max[];\
extern int gap_match_length;
extern int cap_relax;
extern int verbose;

/* enums */
extern char alignments[][20];

/* prototypes */
int find_sync(BYTE ** gcr_pptr, BYTE * gcr_end);
void convert_4bytes_to_GCR(BYTE * buffer, BYTE * ptr);
int convert_4bytes_from_GCR(BYTE * gcr, BYTE * plain);
int extract_id(BYTE * gcr_track, BYTE * id);
int extract_cosmetic_id(BYTE * gcr_track, BYTE * id);
size_t find_track_cycle(BYTE ** cycle_start, BYTE ** cycle_stop, size_t cap_min, size_t cap_max);
size_t find_raw_track_cycle(BYTE ** cycle_start, BYTE ** cycle_stop, size_t cap_min, size_t cap_max);
BYTE convert_GCR_sector(BYTE * gcr_start, BYTE * gcr_end, BYTE * d64_sector, int track, int sector, BYTE * id);
void convert_sector_to_GCR(BYTE * buffer, BYTE * ptr, int track, int sector, BYTE * diskID, int error);
BYTE * find_sector_gap(BYTE * work_buffer, size_t tracklen, size_t * p_sectorlen);
BYTE * find_sector0(BYTE * work_buffer, size_t tracklen, size_t * p_sectorlen);
size_t extract_GCR_track(BYTE * destination, BYTE * source, BYTE *align, int halftrack, size_t cap_min, size_t cap_max);
int replace_bytes(BYTE * buffer, size_t length, BYTE srcbyte, BYTE dstbyte);
size_t check_bad_gcr(BYTE * gcrdata, size_t length);
BYTE check_sync_flags(BYTE * gcrdata, int density, size_t length);
void bitshift(BYTE * gcrdata, size_t length, int bits);
size_t check_errors(BYTE * gcrdata, size_t length, int track, BYTE * id, char * errorstring);
size_t check_empty(BYTE * gcrdata, size_t length, int track, BYTE * id, char * errorstring);
size_t compare_tracks(BYTE * track1, BYTE * track2, size_t length1, size_t  length2, int same_disk, char * outputstring);
size_t compare_sectors(BYTE * track1, BYTE * track2, size_t length1, size_t length2, BYTE * id1, BYTE * id2, int track, char * outputstring);
size_t strip_runs(BYTE * buffer, size_t length, size_t length_max, size_t minrun, BYTE target);
size_t reduce_runs(BYTE * buffer, size_t length, size_t length_max, size_t minrun, BYTE target);
size_t strip_gaps(BYTE * buffer, size_t length);
size_t reduce_gaps(BYTE * buffer, size_t length, size_t length_max);
size_t is_bad_gcr(BYTE * gcrdata, size_t length, size_t pos);
int check_formatted(BYTE * gcrdata, int length);
int check_valid_data(BYTE * data, int matchlen);
char topetscii(char s);
char frompetscii(char s);

