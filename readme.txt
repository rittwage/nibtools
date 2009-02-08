README.TXT for the NIBTOOLS utilities 0.50 (October 11, 2007)

homepage: http://c64preservation.com/nibtools

NIBTOOLS are copyrighted
(C) 2005-07 Pete Rittwage

It is based on MNIB which is copyrighted
(C) 2000-03 Markus Brenner


========================================
= Introduction                         =
========================================

   NIBTOOLS is a disk transfer program designed for copying original disks 
   and converting into the G64 and D64 disk image formats. These disk images
   may be used on C64 emulators like VICE or CCS64 [2,3] and can be transferred
   back to real disks.

   REQUIREMENTS:

   - Commodore Disk Drive model 1541, 1541-II or 1571, modified to support
     the parallel XP1541 or XP1571 interface [1]

   - XE1541, XA1541, or XM1541 cable [1]

   - Microsoft DOS and cwsdpmi.exe software, Linux with OpenCBM, or
     Windows NT/2000/XP with OpenCBM.


========================================
= Usage                                =
========================================

Reading real disks into disk images:

   1) connect 1541/71 drive to your PC's parallel port(s), using
      the XE1541/XA1541 and the XP1541/71 cables.

   2) insert disk into drive and start NIBTOOLS:
      nibread [options] filename.nib

   3) use nibconv to convert between different formats:

      nibconv filename.nib filename.g64
      nibconv filename.nib filename.d64

Writing back disk images to a real disk:

   1) connect 1541/71 drive to your PC's parallel port(s), using
      the XE1541/XA1541 and the XP1541/71 cables.

   2) insert destination disk into drive and start NIBTOOLS:
     
      nibwrite filename.nib
      nibwrite filename.g64
      nibwrite filename.d64


========================================
= Tips and Tricks                      =
========================================


   Please support me!
   ------------------

   For further development of NIBTOOLS it is *vital* that I get feedback
   from you, the users! Please send me reports about your usage of
   NIBTOOLS. I want to know about problems, as well as success and failures
   to convert Original disks to G64/D64 images.

   If you own a stack of original disks and plan to convert them
   using NIBTOOLS, PLEASE DROP ME A MAIL - I would love to get and
   analyze your NIB images, working as well as non-working, to improve
   NIBTOOLS's success rate for future versions.

   If you send me your NIB images I will gladly add your name to the
   Thank You! list at the end of this document :-)
   

   Success Rate on Originals
   -------------------------

   Currently, I estimate NIBTOOLS's 'success rate' on successfully
   copying copy protected games into working G64 images at about
   95%.

   The following table gives an overview over protection schemes
   and NIBTOOLS's chances on copying them:

   Copy Protection          D64     G64     Used by

   Read Errors              X       X	    years ca. 1983-1985
   Tracks 35-40             X       X       Firebird, Para Protect
   Half Tracks                      X	    Big Five (Bounty Bob Strikes Back), System 3
   Wide Tracks                      X       early EA, Activision
   Long/Custom Tracks               X       Datasoft, Mindscape
   Slowed down motor                X       v-MAX!, Later Vorpal
   Sync counting                    X       Epyx (early Vorpal)
   Nonstandard bitrates             X       V-MAX!, Rapidlok
   Bitrate changes in track
   NO sync marks	            X	    later EA (Pirateslayer)      
   ALL sync marks (killer)	    X	    various
   Sector synchronization           X       Rapidlok
   00 Bytes                         X       Datasoft, Rainbow Arts

   Not all of these may run on the current emulators. Disk emulation
   still isn't perfect, especially some of the more tricky protections
   (sector synchronization, Bitrate changes) are not yet
   fully implemented by VICE and CCS64.  Later versions of CCS now do
   support 00 bytes, as well as patched versions of VICE.

   ---

   usage: nibread/nibwrite [options] filename
   (some options are for reading only, some are for writing only, some are for both)

   -D[n] : Drive # (default 8)

   -S[n] : Starting Track (default 1)

   -E[n] : Ending Track (default 41)

   -s[n] : Track skew in microseconds - Some protections depend on data being perfectly aligned from
           track to track.  Some depend on them being skewed a specific amount from each other.  You 
           can use this feature to reproduce this if you know the skew.  There is a tool to determine
           the skew in OpenCBM called rpm1541.

   -t : Timer-based track alignment.  Used to simulate track to track alignment using tightly controlled
        delays.  It can be accurate to 10ms or so on a stable drive, nearly useless on others.  

   -u : Unformat disk (removes *ALL* data) This option writes all $00 bytes (bad GCR) to the entire disk
	surface, simulating the state of a brand new never-formatted disk.

   -l : Limit functions to 40 tracks (R/W) Some disk drives will not function past track 41 and will click
	and jam the heads too far forward. The drive cover must then be removed and the head pushed back
	manually. If this happens to you, use this option with every operation. There are only a few disks
	which utilize track 41 for protection.

   -h : Use halftracks (R/W) This option will step the drive heads 1/2 track at a time during disk
	operations instead of a full track. This protection is only very rarely used.  I have only found
        2 disks out of thousands.  Bounty Bob Strikes Back is one.

   -k : Disable reading 'killer' tracks (R) Some drives will timeout when trying to read tracks that consist
	of all sync. If you cannot read a disk because of timeouts, use this option.

   -r : Disable 'reduce syncs' option (R) By default, NIBTOOLS will "compress" a track when writing back out to
	a disk if the track is longer than what your drive can write at any given density (due to drive
	motor speed). Some (very rare) protections count sync lengths so the protection might fail with this
	option. For 99.9% of disks, it is fine and is the default setting.

   -g : Enable 'reduce gaps' option (R) This option is another form of "compression" used when writing out a
	disk. "gaps" are inert data placed right before a sync mark that can usually be safely removed. It
	is not on by default, but if NIBTOOLS is truncating tracks and they still won't load, you can try this
	option to squeeze a bit more onto the track.

   -0 : Enable 'reduce bad GCR' option (R) This option is another form of "compression" used when writing out a
	disk. "Bad GCR" (when not used for copy protection) is unformatted or corrupted data that can
	usually be safely removed. It is not on by default, but if NIBTOOLS is truncating tracks and they still
	won't load, you can try this option to squeeze a bit more onto the track.

   -f : Disable bad GCR detection (W) "bad GCR" is either corrupted (or illegal) GCR that are
	either intentionally placed on a disk for protection, or are simply unformatted data on the disk.
	NIBTOOLS will by default zero out this data and write it to disk as if it were unformatted. This option
	can be disabled if the disk image is using illegal GCR on purpose, such as how V-MAX! does on the
	track 20 loader.

   -c : Disable automatic capacity adjustments.  By default NIBTOOLS measures the speed of your drive and makes
        adjustments to the data (compression) based on that speed.  If your drive is exactly 300rpm or the
        tracks you are writing are standard (D64), you can bypass this and save a few seconds.

   -aX: Alternative track alignments (W) There are several different ways to align tracks when writing them
	back out. By default, NIBTOOLS will do it's best to figure out how the original disk had it by analyzing
	the data. To force other methods, use this option. 
	
	-aw: Align all tracks to the longest run of unformatted data. 
	-ag: Align all tracks to the longest gap between sectors. 
	-a0: Align all tracks to sector 0. 
	-as: Align all tracks to the longest sync mark. 
	-aa: Align all tracks to the longest run of any one byte (autogap).

   -eX: Extended read retries (R) This is used on deteriorated disks to increase the number of read attempts
	to get a track with no errors. Use any numerical value, but if it's too high it could take a while
	to read the disk. Default is 10.

   -pX: Custom protection handlers (W) This is used to set some flags to handle copy protections which don't
	remaster with default settings. 
        
        -px: Used for V-MAX disks to remaster track 20 properly. 
	-pg: Used for GMA/Securispeed disks to remaster track 38 properly. 
	-pr: Used for Rapidlok disks to remaster them properly. 
	-pv: Used for newer Vorpal disks, which must be custom aligned to load when remastered.

   -G[n] : Match track gap by [n] bytes.  By default the pattern matching looks for repeating 
	   patterns of 7 (56 bits) bytes to find the gaps.  You can adjust this if you are getting too small
           track length detection (or too large).

   -d : Force default densities.  By default NIBTOOLS tries to detect the density of the written data.  If
        you're sure the disk is standard, you can use this to bypass the checks and save time.  This is useful
        because sometimes badly damaged tracks can detect at the wrong density.

   -v : Verbose.  Output more detailed data to console.

   -m : Enable track matching.  This is a crude read verification

   -i : Interactive mode.  This allows for reading many disks in one sitting without having to initialize
        the disk drive every time.  Imaging a disk in this way takes about 8 seconds for a full 41 tracks.


   Why Does it Bump?
   -----------------

   At the beginning of each disk transfer NIBTOOLS issues a 'bump' command.
   This is necessary to guarantee an optimal track adjustment of the
   read head. As NIBTOOLS can't rely on sector checksums, there's no other
   way on adjusting the head-to-track alignment but bumping. Sorry!


========================================
= History                              =
========================================

   0.17 added automatic 1541/157x drive type detection 
   0.18 tracks 36-41 added. No more crashing with unformatted tracks. 
   0.19 added Density and Halftrack command line switches (-d, -h) 
   0.20 added Bump and Reset options (-b, -r) 
   0.21 added timeout routine for nibble transfer 
   0.22 added flush command during reading 
   0.23 disable interrupts during serial protocol 
   0.24 improved serial protocol 
   0.25 hopefully fixed nibbler hangups 
   0.26 added 'S' track reading (read without waiting for Sync) 
   0.27 added hidden 'g' switch for GEOS 1.2 disk image 
   0.28 improved killer track detection, fixed some n2d and n2g bugs 
   0.29 added direct D64 nibble functionality 
   0.30 added D64 error correction by multiple read 
   0.31 added 40 track support for D64 images 
   0.32 bin-include bn_flop.prg 
   0.33 improved D64 mode, added g2d utility to archive 
   0.34 improved track cycle detection
   0.35 added XA1541 support, paranoid mode, first public release
   0.36 Program mostly rewritten and made cross-platform
   0.40 Final versions of MNIB codebase
   0.50 Renamed to NIBTOOLS and most code rearranged for easier image support

========================================
= References                           =
========================================

  The latest version of this program is available on
  http://c64preservation.com/nibtools

  [1] Circuit-diagrams and order form for the adaptor and cables
      http://sta.c64.org/cables.html  (diagrams and shop for X-cables)
      http://sta.c64.org/xe1541.html  (XE1541 cable)
      http://sta.c64.org/xa1541.html  (XA1541 cable)
      http://sta.c64.org/xp1541.html  (XP1541/71 cables)

  [2] CCS64 homepage
      http://www.computerbrains.com/ccs64/

  [3] VICE homepage
      http://http://viceteam.bei.t-online.de/


   "Thank you!" to all people who helped me out with information and
   testing

   - Andreas Boose         
   - Joe Forster           
   - Michael Klein        
   - Matt Larsen          
   - Mat Allen (Mayhem)
   - Chris Link            
   - Jerry Kurtz
   - Wolfgang Moser       
   - H†kan Sundell         
   - Nicolas Welte         
   - Tim Schurman
   - Spiro Trikaliotis
   - Nate Lawson
   - Quader
   - Jani