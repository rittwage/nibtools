# NIBTOOLS WSDK Makefile v1.00
#
# Copyright 2009 Arnd Menge <arnd(at)jonnz(dot)de>
#
# For use with
#
#   NIBTOOLS   v0.5.9    -- http://www.rittwage.com        (SVN Rev. 244 source code)
#   OPENCBM    v0.4.3rc2 -- http://opencbm.trikaliotis.net (Win32/64 binaries for the target platforms)
#   CC65 Win32 v2.13.0-1 -- http://www.cc65.org            (Win32 binary release, "cc65-win32-2.13.0-1.zip")
#
# and
#
#   Microsoft Windows Software Development Kit (SDK)
#      for Windows 7 and .NET Framework 3.5 SP1
#               v7.0.7600.16385.40715
#              http://www.microsoft.com
#
# Test environment(s):
#
#   CPU: Intel T2060 32bit / OS: Microsoft Windows 7 Ultimate 32bit
#
# HOW TO USE this Makefile:
# =========================
#
#   (1) Make sure you have all files listed in the directory/files list
#       section below and organized them as shown.
#
#   (2) Start a Windows SDK "CMD Shell".
#
#   (3) Run "Setenv" to configure your Build Environment
#
#       Usage:
#       Setenv [/release | /debug] [/x86 | /x64]
#
#       E.g.:
#       "Setenv /release /x86"  : RELEASE Build, 32bit x86 applications
#       "Setenv /release /x64"  : RELEASE Build, 64bit x64 applications
#
#   (4) Change to this Makefile's directory.
#
#   (5) Run this Makefile by "NMAKE", or "NMAKE <target>".
#
#       Targets are:
#         all       -- Builds everything.
#         nibread   -- Builds nibread only.
#         nibwrite  -- Builds nibwrite only.
#         nibconv   -- Builds nibconv only.
#         nibrepair -- Builds nibrepair only.
#         nibscan   -- Builds nibscan only.
#         clean     -- Cleanup (deletes output files and directories
#                                     of currently selected platform).
#
#       All output files are placed into the output directories defined in
#       "Project directories" section below (e.g. "WIN7_X64_RETAIL").
#       All RELEASE binaries are linked to Microsoft VC9 runtime library
#       "MSVCR90.DLL".
#
# NOTES:
# ======
#
#   All NIBTOOLS files are required unmodified original, fetched from
#   official SVN.
#   The resource files "WIN-Resource\*.rc" must be copies of "WINBUILD*\*.rc".
#
#   You can build for platforms different from yours if you first build
#   Bin2Array utility for your own platform (run this Makefile for your own
#   platform), change Bin2Array's directory below to point to the output
#   directory associated with your own platform, and finally run this
#   Makefile for the target platform.
#
# PROJECT DIRECTORY TREE AND FILE LIST:
# =====================================
#
#   This Makefile applies to the following directory tree and file list.
#
#   All NIBTOOLS files are listed below.
#   For OPENCBM and CC65 only the required files are listed.
#
#   \nibdev\nibtools\cbm.c
#   \nibdev\nibtools\crc.c
#   \nibdev\nibtools\crc.h
#   \nibdev\nibtools\dirs
#   \nibdev\nibtools\drive.c
#   \nibdev\nibtools\fileio.c
#   \nibdev\nibtools\gcr.c
#   \nibdev\nibtools\gcr.h
#   \nibdev\nibtools\kernel.c
#   \nibdev\nibtools\md5.c
#   \nibdev\nibtools\md5.h
#   \nibdev\nibtools\nibconv.c
#   \nibdev\nibtools\nibread.c
#   \nibdev\nibtools\nibrepair.c
#   \nibdev\nibtools\nibscan.c
#   \nibdev\nibtools\nibtls_rt.c
#   \nibdev\nibtools\nibtools.h
#   \nibdev\nibtools\nibtools_15x1.asm
#   \nibdev\nibtools\nibwrite.c
#   \nibdev\nibtools\prot.c
#   \nibdev\nibtools\prot.h
#   \nibdev\nibtools\read.c
#   \nibdev\nibtools\readme.txt
#   \nibdev\nibtools\version.h
#   \nibdev\nibtools\write.c
#   \nibdev\nibtools\GNU\Makefile
#   \nibdev\nibtools\include\DOS\cbm.h
#   \nibdev\nibtools\include\DOS\kernel.h
#   \nibdev\nibtools\include\DOS\mnib_rt.h
#   \nibdev\nibtools\include\DOS\mnibarch.h
#   \nibdev\nibtools\include\LINUX\mnibarch.h
#   \nibdev\nibtools\include\WINDOWS\mnibarch.h
#   \nibdev\nibtools\include\WINDOWS\opencbm.h
#   \nibdev\nibtools\LINUX\Makefile
#   \nibdev\nibtools\WIN-Resource\nibconv.rc
#   \nibdev\nibtools\WIN-Resource\nibread.rc
#   \nibdev\nibtools\WIN-Resource\nibrepair.rc
#   \nibdev\nibtools\WIN-Resource\nibscan.rc
#   \nibdev\nibtools\WIN-Resource\nibwrite.rc
#   \nibdev\nibtools\WINBUILD-nibconv\Makefile
#   \nibdev\nibtools\WINBUILD-nibconv\nibconv.dsp
#   \nibdev\nibtools\WINBUILD-nibconv\nibconv.rc
#   \nibdev\nibtools\WINBUILD-nibconv\sources
#   \nibdev\nibtools\WINBUILD-nibread\Makefile
#   \nibdev\nibtools\WINBUILD-nibread\Makefile.inc
#   \nibdev\nibtools\WINBUILD-nibread\nibread.dsp
#   \nibdev\nibtools\WINBUILD-nibread\nibread.rc
#   \nibdev\nibtools\WINBUILD-nibread\sources
#   \nibdev\nibtools\WINBUILD-nibrepair\Makefile
#   \nibdev\nibtools\WINBUILD-nibrepair\nibrepair.dsp
#   \nibdev\nibtools\WINBUILD-nibrepair\nibrepair.rc
#   \nibdev\nibtools\WINBUILD-nibrepair\sources
#   \nibdev\nibtools\WINBUILD-nibscan\Makefile
#   \nibdev\nibtools\WINBUILD-nibscan\nibscan.dsp
#   \nibdev\nibtools\WINBUILD-nibscan\nibscan.rc
#   \nibdev\nibtools\WINBUILD-nibscan\sources
#   \nibdev\nibtools\WINBUILD-nibwrite\Makefile
#   \nibdev\nibtools\WINBUILD-nibwrite\Makefile.inc
#   \nibdev\nibtools\WINBUILD-nibwrite\nibwrite.dsp
#   \nibdev\nibtools\WINBUILD-nibwrite\nibwrite.rc
#   \nibdev\nibtools\WINBUILD-nibwrite\sources
#   \nibdev\nibtools\WSDK\bin2array.c
#   \nibdev\nibtools\WSDK\Makefile
# 
#   \nibdev\cc65-win32-2.13.0-1\bin\ca65.exe
#   \nibdev\cc65-win32-2.13.0-1\bin\cl65.exe
#   \nibdev\cc65-win32-2.13.0-1\bin\ld65.exe
#
#   \nibdev\opencbm-0.4.3rc2-i386\opencbm.dll
#   \nibdev\opencbm-0.4.3rc2-amd64\opencbm.dll
#
# VERSION HISTORY:
# ================
#
#   v1.00 -- initial Makefile version.
#
# LEGAL NOTICE:
# =============
#
#   THIS FILE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND
#   AND/OR FITNESS FOR A PARTICULAR PURPOSE. USE AT YOUR OWN RISK.
#

# -------------------------------------------------------------------------
# Include Win32 application master NMAKE definitions file
# -------------------------------------------------------------------------

!include <Win32.Mak>

# -------------------------------------------------------------------------
# Print Makefile information
# -------------------------------------------------------------------------

!message NIBTOOLS WSDK Makefile v1.00
!message Copyright 2009 Arnd Menge <arnd(at)jonnz(dot)de>
!message

# -------------------------------------------------------------------------
# Project directories
# -------------------------------------------------------------------------

# NIBTOOLS C source files directory
C_DIR = ..

# Output/working directory
OUTDIR = $(OUTDIR)

# NIBTOOLS binary output directory
BINDIR = $(OUTDIR)

# Include directory ("mnibarch.h" and "opencbm.h")
INCLUDE_DIR = ..\include\WINDOWS

# Resource directory
RC_DIR = ..\WIN-Resource

# "Bin2Array.exe" utility directory
B2A_DIR = $(OUTDIR)
B2A_DIR = XP32_RETAIL

# "opencbm.dll" directories
OPENCBM_DLL_DIR_x86  = ..\..\bin\i386
OPENCBM_DLL_DIR_x64  = ..\..\bin\amd64

# CC65 directory
CC65_BIN_DIR = ..\..\..\cc65\bin

# -------------------------------------------------------------------------
# Build targets
# -------------------------------------------------------------------------

all: $(OUTDIR)\nibread.exe   \
     $(OUTDIR)\nibwrite.exe  \
     $(OUTDIR)\nibconv.exe   \
     $(OUTDIR)\nibrepair.exe \
     $(OUTDIR)\nibscan.exe

nibread  : $(OUTDIR)\nibread.exe
nibwrite : $(OUTDIR)\nibwrite.exe
nibconv  : $(OUTDIR)\nibconv.exe
nibrepair: $(OUTDIR)\nibrepair.exe
nibscan  : $(OUTDIR)\nibscan.exe

# -------------------------------------------------------------------------
# Object lists
# -------------------------------------------------------------------------

BASE_OBJS = $(OUTDIR)\gcr.obj    \
            $(OUTDIR)\prot.obj   \
            $(OUTDIR)\fileio.obj \
            $(OUTDIR)\crc.obj    \
            $(OUTDIR)\md5.obj

NIBREAD_OBJS = $(BASE_OBJS)          \
               $(OUTDIR)\nibread.obj \
               $(OUTDIR)\drive.obj   \
               $(OUTDIR)\read.obj    \
               $(OUTDIR)\nibread.res

NIBWRITE_OBJS = $(BASE_OBJS)           \
                $(OUTDIR)\nibwrite.obj \
                $(OUTDIR)\drive.obj    \
                $(OUTDIR)\write.obj    \
                $(OUTDIR)\nibwrite.res

# -------------------------------------------------------------------------
# Set Target Platform
# -------------------------------------------------------------------------

!IF "$(CPU)" == "i386"
TARGET_CPU = x86
OPENCBM_DLL_DIR = $(OPENCBM_DLL_DIR_x86)

!ELSEIF "$(CPU)" == "AMD64"
TARGET_CPU = x64
OPENCBM_DLL_DIR = $(OPENCBM_DLL_DIR_x64)

!ELSE
!ERROR Unrecognized Target Platform (must be x86 or x64)

!ENDIF

# -------------------------------------------------------------------------
# Print configuration
# -------------------------------------------------------------------------

!message Configuration: ...................................................
!message Current Platform: $(PROCESSOR_ARCHITECTURE)
!message Target Platform: $(TARGET_CPU)
!message Using "$(OPENCBM_DLL_DIR)\opencbm.dll"
!message Using "$(B2A_DIR)\bin2array.exe"
!message ..................................................................
!message

# -------------------------------------------------------------------------
# Create OUTDIR & BINDIR directories if they do not exist
# -------------------------------------------------------------------------

CreateDirs:
    @echo ^ ^ ^ ^ ^ ^ ^ ^ ...... Creating directories .....................................
    if not exist "$(OUTDIR)/$(NULL)" mkdir $(OUTDIR)
!IF "$(OUTDIR)" != "$(BINDIR)"
    if not exist "$(BINDIR)/$(NULL)" mkdir $(BINDIR)
!ENDIF

# -------------------------------------------------------------------------
# If OPENCBM.LIB does not exist, then create it
# -------------------------------------------------------------------------

OpenCBM:
!IF !EXIST ($(OUTDIR)\opencbm.lib)
    @echo ^ ^ ^ ^ ^ ^ ^ ^ ...... Rebuilding OPENCBM.LIB ...................................
    echo EXPORTS > "$(OUTDIR)\opencbm.def"
    for /f "skip=19 tokens=4" %%i in ('dumpbin /exports "$(OPENCBM_DLL_DIR)\opencbm.dll"') do @echo %%i >> "$(OUTDIR)\opencbm.def"
    lib /machine:$(TARGET_CPU) /def:"$(OUTDIR)\opencbm.def" /out:"$(OUTDIR)\opencbm.lib"
!ENDIF
!IF !EXIST ($(BINDIR)\opencbm.dll)
    @echo ^ ^ ^ ^ ^ ^ ^ ^ ...... Copying OPENCBM.DLL ......................................
    copy /y "$(OPENCBM_DLL_DIR)\opencbm.dll" "$(BINDIR)\"
!ENDIF

# -------------------------------------------------------------------------
# Build the drive code files
# -------------------------------------------------------------------------

$(C_DIR)\DriveCode:
    @echo ^ ^ ^ ^ ^ ^ ^ ^ ...... Building 1541/1571 drive codes ...........................

#   Create CC65 compatible assembler source files
    echo DRIVE=1541 > "$(OUTDIR)\nibtools_1541.a65"
    echo DRIVE=1571 > "$(OUTDIR)\nibtools_1571.a65"
    type "$(C_DIR)\nibtools_15x1.asm" >> "$(OUTDIR)\nibtools_1541.a65"
    type "$(C_DIR)\nibtools_15x1.asm" >> "$(OUTDIR)\nibtools_1571.a65"

#   Build 1541/1571 drive code binaries
    "$(CC65_BIN_DIR)\ca65" -t none -l "$(OUTDIR)\nibtools_1541.a65"
    "$(CC65_BIN_DIR)\ca65" -t none -l "$(OUTDIR)\nibtools_1571.a65"
    "$(CC65_BIN_DIR)\cl65" -t none -o "$(OUTDIR)\nibtools_1541.bin" "$(OUTDIR)\nibtools_1541.o"
    "$(CC65_BIN_DIR)\cl65" -t none -o "$(OUTDIR)\nibtools_1571.bin" "$(OUTDIR)\nibtools_1571.o"

#   Build "bin2array.exe" utility
    $(cc) $(cdebug) $(cflags) $(cvarsdll) /wd4996 /Fo"$(OUTDIR)/" /Fd"$(OUTDIR)/vc90.pdb" bin2array.c
    $(link) $(ldebug) $(conlflags) $(conlibsdll) -out:"$(OUTDIR)\bin2array.exe" "$(OUTDIR)\bin2array.obj"
    mt.exe -manifest "$(OUTDIR)\bin2array.exe.manifest" -outputresource:"$(OUTDIR)\bin2array.exe";1

#   Convert 1541/1571 drive code binaries to C include files
    $(B2A_DIR)\bin2array "$(OUTDIR)\nibtools_1541.bin" "$(C_DIR)\nibtools_1541.inc"
    $(B2A_DIR)\bin2array "$(OUTDIR)\nibtools_1571.bin" "$(C_DIR)\nibtools_1571.inc"

    @echo ^ ^ ^ ^ ^ ^ ^ ^ .................................................................
    @echo ^ ^ ^ ^ ^ ^ ^ ^ .................................................................

# -------------------------------------------------------------------------
# Update the object files if necessary
# -------------------------------------------------------------------------

{$(C_DIR)}.c{$(OUTDIR)}.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsdll) /wd4996 /O2 /I"$(INCLUDE_DIR)" /Fo"$(OUTDIR)/" /Fd"$(OUTDIR)/vc90.pdb" $**

# -------------------------------------------------------------------------
# Update the resource files if necessary
# -------------------------------------------------------------------------

{$(RC_DIR)}.rc{$(OUTDIR)}.res:
    $(rc) $(rcflags) $(rcvars) /I"$(C_DIR)" /Fo"$(OUTDIR)/%|fF.res" $**

# -------------------------------------------------------------------------
# Update the executable files if necessary
# -------------------------------------------------------------------------

$(OUTDIR)\nibread.exe: CreateDirs OpenCBM $(C_DIR)\DriveCode $(NIBREAD_OBJS)
    $(link) $(ldebug) $(conlflags) $(conlibsdll) -out:"$(BINDIR)\nibread.exe" $(NIBREAD_OBJS) "$(OUTDIR)\opencbm.lib"
    mt.exe -manifest "$(BINDIR)\nibread.exe.manifest" -outputresource:"$(BINDIR)\nibread.exe";1

$(OUTDIR)\nibwrite.exe: CreateDirs OpenCBM $(C_DIR)\DriveCode $(NIBWRITE_OBJS)
    $(link) $(ldebug) $(conlflags) $(conlibsdll) -out:"$(BINDIR)\nibwrite.exe" $(NIBWRITE_OBJS) "$(OUTDIR)\opencbm.lib"
    mt.exe -manifest "$(BINDIR)\nibwrite.exe.manifest" -outputresource:"$(BINDIR)\nibwrite.exe";1

$(OUTDIR)\nibconv.exe: CreateDirs $(OUTDIR)\nibconv.obj $(OUTDIR)\nibconv.res $(BASE_OBJS)
    $(link) $(ldebug) $(conlflags) $(conlibsdll) -out:"$(BINDIR)\nibconv.exe" "$(OUTDIR)\nibconv.obj" "$(OUTDIR)\nibconv.res" $(BASE_OBJS)
    mt.exe -manifest "$(BINDIR)\nibconv.exe.manifest" -outputresource:"$(BINDIR)\nibconv.exe";1

$(OUTDIR)\nibrepair.exe: CreateDirs $(OUTDIR)\nibrepair.obj $(OUTDIR)\nibrepair.res $(BASE_OBJS)
    $(link) $(ldebug) $(conlflags) $(conlibsdll) -out:"$(BINDIR)\nibrepair.exe" "$(OUTDIR)\nibrepair.obj" "$(OUTDIR)\nibrepair.res" $(BASE_OBJS)
    mt.exe -manifest "$(BINDIR)\nibrepair.exe.manifest" -outputresource:"$(BINDIR)\nibrepair.exe";1

$(OUTDIR)\nibscan.exe: CreateDirs $(OUTDIR)\nibscan.obj $(OUTDIR)\nibscan.res $(BASE_OBJS)
    $(link) $(ldebug) $(conlflags) $(conlibsdll) -out:"$(BINDIR)\nibscan.exe" "$(OUTDIR)\nibscan.obj" "$(OUTDIR)\nibscan.res" $(BASE_OBJS)
    mt.exe -manifest "$(BINDIR)\nibscan.exe.manifest" -outputresource:"$(BINDIR)\nibscan.exe";1

# -------------------------------------------------------------------------
# Cleanup
# -------------------------------------------------------------------------

clean:
    $(CLEANUP)
    if exist "$(C_DIR)\nibtools_1541.inc"     del "$(C_DIR)\nibtools_1541.inc"
    if exist "$(C_DIR)\nibtools_1571.inc"     del "$(C_DIR)\nibtools_1571.inc"
!IF "$(OUTDIR)" != "$(BINDIR)"
    if exist $(BINDIR)/$(NULL) rd /s /q $(BINDIR)
!ENDIF
