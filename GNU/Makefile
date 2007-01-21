#
# Makefile for building mnib, n2g, n2d, pshift, and pscan
#
# Build targets are:  dos(djgpp) win32(cygwin) linux
#

# If building for DOS, change this to the path to your DJGPP install
DJGPP_PATH=	c:/djgpp

# If building for Linux or Windows, change this to the base path to your
# opencbm includes and libraries
CBM_LNX_PATH=	/usr/local
#CBM_WIN_PATH=	c:\cbm4win
CBM_WIN_PATH=	../

# Default compile flags.  Be careful with optimization (-O2)
CFLAGS=		${WARNS}

# cl65 defs, contributed by Ullrich von Bassewitz <uz(at)musoftware(dot)de>
# (cc65 >= 2.6 required)
CA65        = ca65
LD65        = ld65
CA65_FLAGS  =  --feature pc_assignment

# No user-configurable parts below

.SUFFIXES: .asm .bin .inc

usage:
	@echo Please specify a target: dos win32 linux clean distclean

# Arch-specific targets
dos:
	${MAKE} DJGPP="${DJGPP_PATH}\djgpp.env" \
		CC="${DJGPP_PATH}/bin/gcc" \
		ARCH_OBJ="cbm.o kernel.o nibtools_rt.o" \
		CFLAGS="-I include/DOS/ $(CFLAGS)" \
		EXE=".exe" \
		-f GNU/Makefile \
		buildall

linux:
	${MAKE} CFLAGS="-I include/LINUX/ -I ${CBM_LNX_PATH}/include ${CFLAGS}" \
		LDFLAGS="-L${CBM_LNX_PATH}/lib -lopencbm" \
		-f GNU/Makefile \
		buildall

win32:
	${MAKE} CFLAGS="-I include/WINDOWS/ -I ${CBM_WIN_PATH}/include -D WIN32 -mno-cygwin ${CFLAGS}" \
		LDFLAGS="-mno-cygwin -L${CBM_WIN_PATH}/bin/i386/ -lopencbm" \
		EXE=".exe" \
		-f GNU/Makefile \
		buildall

# Warning level.  Don't reduce, fix your new code instead.
WARNS=	-Werror -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wpointer-arith 

# Common objects
OBJ=gcr.o prot.o 

# Objects for just mnib
NIBREAD_OBJ=nibread.o read.o drive.o
NIBWRITE_OBJ=nibwrite.o write.o drive.o 

NIBTOOLS_BIN=nibtools_1541.inc nibtools_1571.inc

# All programs to build
PROG=nibread nibwrite nibscan nibconv

buildall: ${PROG}

nibread: ${OBJ} ${NIBREAD_OBJ} ${NIBTOOLS_BIN} ${ARCH_OBJ}
	${CC} -o nibread$(EXE) ${OBJ} ${NIBREAD_OBJ} ${ARCH_OBJ} ${LDFLAGS}

nibwrite: ${OBJ} ${NIBWRITE_OBJ} ${NIBTOOLS_BIN} ${ARCH_OBJ}
	${CC} -o nibwrite$(EXE) ${OBJ} ${NIBWRITE_OBJ} ${ARCH_OBJ} ${LDFLAGS}

nibread.c: ${NIBTOOLS_BIN} 

nibwrite.c: ${NIBTOOLS_BIN} 

nibconv: ${OBJ} nibconv.o
	${CC} -o nibconv$(EXE) nibconv.o ${OBJ} $(LDFLAGS)

nibscan:	${OBJ} nibscan.o
	${CC} -o nibscan$(EXE) nibscan.o ${OBJ} $(LDFLAGS)

clean:
	${RM} *.o ${MNIB_BIN} *.bin

distclean: clean
	${RM} ${PROG} *.exe

nibtools_1541.bin: nibtools_15x1.asm
	$(CA65) $(CA65_FLAGS) -D DRIVE=1541 -o $*.tmp $<
	$(LD65) --target none -o $@ $*.tmp && rm -f $*.tmp

nibtools_1571.bin: nibtools_15x1.asm
	$(CA65) $(CA65_FLAGS) -D DRIVE=1571 -o $*.tmp $<
	$(LD65) --target none -o $@ $*.tmp && rm -f $*.tmp

.bin.inc:
	test -s $< && od -w8 -txC -v -An $< | \
	sed 's/\([0-9a-f]\{2\}\) */0x\1,/g; $$s/,$$//' > $@
