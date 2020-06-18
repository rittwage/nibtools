# $Id: Makefile,v 1.1.1.1 2007/01/21 17:15:35 peter Exp $

RELATIVEPATH=../
include ${RELATIVEPATH}LINUX/config.make

.PHONY: all clean

OBJS =  nibread.o nibwrite.o nibscan.o nibconv.o nibrepair.o nibsrqtest.o read.o write.o gcr.o prot.o crc.o drive.o fileio.o ihs.o lz.o md5.o 
PROG = nibread nibwrite nibscan nibconv nibrepair nibsrqtest

all:
	make -f GNU/Makefile CBM_LNX_PATH="../" linux

clean:
	make -f GNU/Makefile CBM_LNX_PATH="../" distclean

include ${RELATIVEPATH}LINUX/prg_install_rules.make
