# $Id: Makefile,v 1.1.1.1 2007/01/21 17:15:35 peter Exp $

RELATIVEPATH=../
include ${RELATIVEPATH}LINUX/config.make

.PHONY: all mrproper clean install uninstall install-files

OBJS =  nibread.o nibwrite.o nibscan.o nibconv.o nibrepair.o nibsrqtest.o read.o write.o gcr.o prot.o crc.o drive.o fileio.o ihs.o lz.o md5.o 
PROG = nibread nibwrite nibscan nibconv nibrepair nibsrqtest

all:
	make -f GNU/Makefile CBM_LNX_PATH="../" linux

mrproper: clean

clean:
	make -f GNU/Makefile CBM_LNX_PATH="../" distclean

install-files: $(PROG)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 -s $(PROG) $(DESTDIR)$(BINDIR)
ifneq "$(words $(MAN1))" "0"
	install -d $(DESTDIR)$(MANDIR)
	install -m 644    $(MAN1) $(DESTDIR)$(MANDIR)
endif
ifneq "$(words $(LINKS))" "0"
	install -d $(DESTDIR)$(BINDIR)
	for l in $(LINKS); do ln -sf $(PROG) $(DESTDIR)$(BINDIR)/$$l; done
	install -d $(DESTDIR)$(MANDIR)
	for l in $(LINKS); do ln -sf $(MAN1) $(DESTDIR)$(MANDIR)/$$l.1; done
endif


install: install-files

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROG)
ifneq "$(words $(MAN1))" "0"
	rm -f $(DESTDIR)$(MANDIR)/$(MAN1){,.gz}
endif
ifneq "$(words $(LINKS))" "0"
	for l in $(LINKS); do rm -f $(DESTDIR)$(BINDIR)/$$l $(DESTDIR)$(MANDIR)/$$l.1{,.gz}; done
endif
