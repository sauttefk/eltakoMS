# Makefile fuer ttylog
# See README for further details
# Copyright (c) 1996-1999 Harald Milz (hm@seneca.muc.de)

# Pfad anpassen:
BINDIR	= /usr/local/sbin

# Compiler und Optionen anpassen:
CC	= gcc
CFLAGS	= -O2 -Wall

# Haben wir TERMIO (SYSV) oder TERMIOS (POSIX) ?
TERMIO	= -DHAVE_TERMIOS
# TERMIO = -DHAVE_TERMIO

# wo liegt install?
INSTALL	= /usr/bin/install	# Linux FSSTND
# INSTALL = /usr/bin/installbsd	# AIX V4
# INSTALL = /usr/ucb/install	# AIX V3

# Format des Lockfiles
#UUCPLOCK = -DUUCPLOCKBINARY
UUCPLOCK = -DUUCPLOCKASCII


# end of configurable options
all: eltakoMS

eltakoMS:	eltakoMS.c version.h 
	$(CC) $(CFLAGS) -o eltakoMS eltakoMS.c

version.h:
	@echo \#define VERSION \"`date "+%d%m%y"`\" > version.h 

install: eltakoMS
	$(INSTALL) -s -m 750 ttylog $(BINDIR)

clean:
	rm -f *.o *~ ttylog core *.bak version.h 
