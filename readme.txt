README.TXT for the NIBTOOLS utilities (Updated 2/16/2014)

homepage: https://diskpreservation.com/dp.php?pg=nibtools

NIBTOOLS is copyrighted
(C) 2005 Pete Rittwage 

It is originally based on MNIB which is copyrighted
(C) 2000 Markus Brenner

In addition, NIBTOOLS at least contains code and/or bug fixes contributed by:
   - Wolfgang Moser       
   - Spiro Trikaliotis
   - Nate Lawson
   - Arnd Menge

========================================
= Introduction                         =
========================================

   NIBTOOLS is a disk transfer program designed for imaging original disks 
   and converting into the G64 and D64 disk image formats. These disk images
   may be used on C64 emulators like VICE or CCS64 [2,3] and in many cases 
   can be transferred back to real disks.

   REQUIREMENTS:

   - Commodore Disk Drive model 1541, 1541-II or 1571, modified to support
     the parallel XP1541 or XP1571 interface [1]

   - XUM1541 (ZoomFloppy) with a 1541+Parallel cable, OR a 1571 with no parallel cable needed.
	* OR * 
   - XEP1541, XAP1541, or XMP1541 combination cable [1]
	* OR * 
   - XP1541 or XP1571 cable and XE1541, XA1541, or XM1541 cable [1]
		
   - Windows, x64 or x86 Editions, with OpenCBM 0.4.2 or higher (latest versions always recommended)
     Linux with OpenCBM 0.4.0 or higher,
     MS/DR/Caldera DOS and cwsdpmi.exe software (no longer tested but still compiles with DJGPP for old <=P3 hardware)
     
========================================
= Usage                                =
========================================

Reading real disks into disk images:

   1) connect 1541/71 drive to your PC's parallel port(s), using the above cabling.

   2) insert disk into drive and start NIBTOOLS:
       nibread [options] filename.nib

   3) use nibconv to convert between different formats:
       nibconv filename.nib filename.g64
       nibconv filename.nib filename.d64

       nibconv filename.nbz filename.g64
       nibconv filename.nbz filename.d64

	nibconv filename.d64 filename.g64
	nibconv filename.g64 filename.d64

Writing back disk images to a real disk:

   1) connect 1541/71 drive to your PC's parallel port(s), using the above cabling.

   2) insert destination disk into drive and start NIBTOOLS:
       nibwrite filename.nib
       nibwrite filename.nbz
       nibwrite filename.g64
       nibwrite filename.d64

========================================
= Tips and Tricks                      =
========================================

   Please support us!
   ------------------

   For further development of NIBTOOLS it is *vital* that we get feedback
   from you, the users! Please send me reports about your usage of
   NIBTOOLS. We want to know about problems, as well as success and failures
   to convert Original disks to G64/D64 images.

   If you own a stack of original disks and plan to convert them
   using NIBTOOLS, PLEASE DROP ME A MAIL - we would love to get and
   analyze your NIB images, working as well as non-working, to improve
   NIBTOOLS's success rate for future versions.

   If you send us your NIB images I will gladly add your name to the
   Thank You! list at the end of this document :-)
   

   Success Rate on Originals
   -------------------------

   Currently, I estimate NIBTOOLS's 'success rate' on successfully
   copying copy protected games into working G64 images at about
   99%.
   
   Writing back to disks using the same hardware has less success, since 
   copy protection was designed to take advantage of the fact that you 
   cannot write everything you can read with any disk drive. Still, you
   can write back the large majority of software successfully.

   The following table gives an overview over protection schemes
   and NIBTOOLS's chances on copying them:

   Copy Protection          D64     G64     Used by

   Read Errors              X       X	  years ca. 1983-1985
   Tracks 35-40             X       X     Firebird, Para Protect
   Half Tracks                      X	  Big Five (Bounty Bob Strikes Back)
   Wide/Fat Tracks                  X     early EA, Activision XEMAG
   Long/Custom Tracks               X     Datasoft, Mindscape
   Slowed down motor                X     V-MAX!, Later Vorpal
   Sync counting/anomalies          X     Early Vorpal
   Nonstandard bitrates             X     V-MAX!, Rapidlok
   Bitrate changes in track	    X	  Software Toolworks (Chessmaster 2100, etc.) (Cannot be written)
   NO sync marks	            X	  later EA (Pirateslayer), later Vorpal
   SHORT sync marks (10 bits)	    X     V-MAX!
   ALL sync marks (killer)	    X	  Br0derbund
   track/sector synchronization     X     Rapidlok
   Weak bits                        X     Rapidlok, Datasoft, Rainbow Arts, Mindscape

   Not all of these may run on the current emulators. Disk emulation
   still isn't perfect, especially some of the more tricky protections
   (sector synchronization, Bitrate changes, bad GCR) are not yet
   fully implemented by all versions of or all emulators.

   ---

   usage: nibread/nibwrite [options] filename
   (some options are for reading only, some are for writing only, some are for both)

   -D[n] : Drive # (default 8)

   -S[n] : Starting Track (default 1)

   -E[n] : Ending Track (default 41)

   -P    : Force 1571 to use parallel instead of SRQ

   -T    : Track skew in microseconds - Some protections depend on data being perfectly aligned from
           track to track.  Some depend on them being skewed a specific amount from each other.  You 
           can use this feature to reproduce this if you know the skew.  There is a tool to determine
           the skew of original disks in OpenCBM called rpm1541.

   -t 	 : Timer-based track alignment.  Used to simulate track to track alignment using tightly controlled
           delays. It can be accurate to 10ms or so on a stable drive, nearly useless on others.  

   -u[n] : Unformat disk for [n] passes (removes *ALL* data) This option alternates writing all sync, then 
	   all $00 bytes (bad GCR) to the entire disk surface, simulating the state of a brand new never-formatted disk.

   -l    : Limit functions to 40 tracks (R/W) Some disk drives will not function past track 41 and will click
	   and jam the heads too far forward. The drive cover must then be removed and the head pushed back
	   manually. If this happens to you, use this option with every operation. There are only a few disks
	   which utilize track >=41 for protection.

   -h 	 : Toggle halftracks (R/W) This option will step the drive heads 1/2 track at a time during disk
	   operations instead of a full track. This protection is only very rarely used.  I have only found
            a few disks out of thousands. Bounty Bob Strikes Back is one.

   -k 	 : Disable reading 'killer' tracks (R) Some drives will timeout when trying to read tracks that consist
	   of all sync. If you cannot read a disk because of timeouts, use this option.

   -r[n] : Disable or modify 'reduce syncs' option (R) 
	   By default, NIBTOOLS will reduce syncs on a track when writing back out to
	   a disk if the track is longer than what your drive can write at any given density (due to drive
	   motor speed). Some protections count sync lengths so the protection might fail with this
	   option. For 99% of disks, it is fine and is the default setting.
	 
	   * You can specify a minimum sync length to leave behind in bytes using [n]

   -F[n] : Creates a "FAT" track in the output image when used with nibconv, on track [n]+0.5,[n]+1.
	   By default, it will attempt to detect a FAT track by comparing GCR data on consequetive tracks.
	   Most fat tracks are autodetected, but not all.

   -g  	 : Enable 'reduce gaps' option (R) This option is another form of data reduction used when writing out an
	   oversized track. Gaps are inert data placed right before a sync mark that can usually be safely 
	   removed, but it's possible to remove too much and damage data, so this is off by default. 
	   If NIBTOOLS is truncating tracks and they still won't load, you can try this option to squeeze
	   a bit more onto the track.  

   -0  	 : Enable 'reduce bad GCR' option (R) This option is another form of data reduction used when writing out an 
	   oversized track. "Bad GCR" (when not used for copy protection) is unformatted or corrupted data that can
	   usually be safely removed. It is not on by default, but if NIBTOOLS is truncating tracks and they still
	   won't load, you can try this option to squeeze a bit more onto the track.

   -f[n] : Modify the "fixing" of bad GCR (W) - "Bad GCR" is either corrupted (or illegal) GCR that are
	   either intentionally placed on a disk for protection, or are simply unformatted data on the disk.
	   NIBTOOLS will by default write 0x00 bytes to the disk to simulate this.  Some protections
	   check this data to see that it is unformatted (drive will read semi-random values). This option
	   can be disabled if the program is using illegal GCR as part of regular data, such as 
	   some V-MAX track 20 loaders.

	   * You can specify an aggression level as [n]
	    0 = do not repair detected bad GCR
	    1 = kill only completely bad GCR bytes (default)
		(after one bad byte has already passed)
	    2 = kill completely bad GCR bytes and "mask out" the bad GCR in bytes preceding and following them
		(after one bad one has already passed)
	    3 = kill bad GCR bytes as well as the bytes preceding and following them
		(even if this is the first bad GCR byte encountered)

   -c 	 : Disable automatic capacity adjustments.  By default NIBTOOLS measures the speed of your drive and make
           adjustments to the data based on that speed. If your drive is exactly 300rpm or the
           tracks you are writing are standard (D64), you can bypass this and save a few seconds.

   -aX 	 : Alternative track alignments (W) There are several different ways to align tracks when writing them
	   back. By default, NIBTOOLS will do it's best to figure out how the original disk was aligned by analyzing
	   the track data. To force other methods, use this option. 
	
	   -aw: Align all tracks to the longest run of unformatted data. 
	   -ag: Align all tracks to the longest gap between sectors. 
	   -a0: Align all tracks to sector 0. 
	   -as: Align all tracks to the longest sync mark (needed for UXB/Melbourne House protection)
	   -aa: Align all tracks to the longest run of any one byte (autogap).
	   -an: Align all tracks to the raw data as read (not normally used).

   -eX	 : Extended read retries (R) This is used on deteriorated disks to increase the number of read attempts
	   to get a track with no errors. Use any numerical value, but if it's too high it could take a while
	   to read the disk. Default is 10.

   -pX	 : Custom protection handlers (W) This is used to set some flags to handle copy protections which don't
	   write properly with default settings. 
        
           -px: Used for V-MAX disks to remaster track 20 properly and align custom tracks. 
	   -pg: Used for GMA/Securispeed disks to remaster track 38/39 properly.
	   -pm: Used for older Rainbow Arts/Magic Bytes to remaster track 36 properly 
	   -pr: Used for Rapidlok disks to help remaster them properly (limited success without patches). 
	   -pv: Used for newer Vorpal disks, which must be custom aligned when remastered.

   -G[n] : Match track gap by [n] bytes.  By default the pattern matching looks for repeating 
	   patterns of 7 (56 bits) bytes to find the gaps.  You can adjust this if you are getting too small
           track length detection (or too large).

   -d 	 : Force default densities.  By default NIBTOOLS tries to detect the density of the written data.  If
           you're sure the disk is standard, you can use this to bypass the checks and save time. This is useful
           because sometimes badly damaged tracks can be detected at the wrong density.

   -v 	 : Verbose. Output more detailed data to console. Specify multiple times (-v -v) for more info.

   -V 	 : Enable raw track matching. This is a raw read verification, mostly for detecting bad floppy disks.

   -I 	 : (When used with nibread) Interactive mode.  This allows for reading many disks in one sitting without having to initialize
       	   the disk drive every time.  Imaging a disk in this way takes about 8 seconds for a full 41 tracks.

   -I 	 : (When used with nibconv, nibwrite) "Fix" too short syncs.  Sometimes when reading, we detect a short sync (9 bits instead of 10) and the
	   1541 can't find the headers when written back out.  This will correct that (crudely) at the cost of making the track
  	   slightly longer.

   -i 	 : Utilize index hole sensor on the 1571 drive, or the "Super-Card+" index hole circuit in any drive.  
	   This works for read/write on side 1 *ONLY*. It will lock up if you try to do this on the flipside 
	   of a disk, because it will never see the index hole.
	   This also does not work in SRQ mode.

   -b[x] : Force custom "fill" byte to use for overlap and filling empty space.  
	   Default is automatically using the last byte of the detected track cycle
	   Other useful ones are "00" for "bad" GCR, "55" for 0x55 (inert data), or "FF" for sync.
   
   -C[n] : Simulate a certain track capacity (given [n] as motor RPM, default 295 to be safe) used when converting to G64.  
	   You can use this to see what happens when creating a G64 with regards to compression/truncation that happens
	   when writing to a real disk with a motor at that RPM.  Accepts any number, but only numbers around 300 make
	   much sense to try. The max a G64 track can be is 7928 bytes (in VICE) and you'll get a damaged track if 
	   you go less than about 290, due to data truncation.

	  NOTE: When writing a G64 file, we don't generally care about the limitations of drive hardware
		However, VICE (previous to version 2.2) ignored the G64 header and hardcoded 7928 as the largest
		track size, and also required it to be 84 tracks no matter if they're used or not.
		Newer versions of VICE don't have this limit, but other emulators might.
		Due to how a NIB file is constructed, no track could ever be longer than 8192 bytes, 
	   	which is around 282 RPM. This never occurs in the real world.

   Why Does it Bump?
   -----------------

   At the beginning of each disk transfer NIBTOOLS issues a 'bump' command.
   This is necessary to guarantee an optimal track adjustment of the
   read head. As NIBTOOLS can't rely on sector checksums, there's no other
   way on adjusting the head-to-track alignment but bumping. Sorry!


========================================
= References                           =
========================================

  [1] Circuit-diagrams and order form for the adaptor and cables
      http://sta.c64.org/cables.html  (diagrams and shop for X-cables)
      http://sta.c64.org/xe1541.html  (XE1541 cable)
      http://sta.c64.org/xa1541.html  (XA1541 cable)
      http://sta.c64.org/xp1541.html  (XP1541/71 cables)

  [2] CCS64 homepage
      http://www.computerbrains.com/ccs64/

  [3] VICE homepage
      http://viceteam.org/


   "Thank you!" to all people who helped me out with information and
   testing

   - Andreas Boose         
   - Joe Forster           
   - Michael Klein        
   - Matt Larsen          
   - Mat Allen (Mayhem)
   - Chris Link            
   - Jerry Kurtz      
   - Hkan Sundell         
   - Nicolas Welte         
   - Tim Schurman
   - Joerg Droege
   - Quader
   - Jani
   - LordCrass
