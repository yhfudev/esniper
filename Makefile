CFLAGS = -O

# System dependencies

# Solaris (2.6 and newer)
#LIBS = -lsocket -lnsl

# Solaris 2.5.1
#LIBS = -lsocket -lnsl -lgen

# HP-UX 10.20
#CFLAGS = -O -D_XOPEN_SOURCE_EXTENDED

# Digital UNIX (OSF1 V4.0)
#CFLAGS = -O -D_XOPEN_SOURCE_EXTENDED

# install is typically in /usr/bin or /usr/sbin
#INSTALL=/usr/bin/install
#INSTALL=/usr/sbin/install
INSTALL=install

# Change this if you want esniper installed elsewhere
INSTALL_DIR=/usr/local/bin

# strict checking options
# Note: -O needed for uninitialized variable warning (part of -Wall)
#
# Flags not included:
#	-Wshadow -Wtraditional -Wid-clash-len -Wredundant-decls
CHECKFLAGS = -O -pedantic -Wall -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wwrite-strings -Wconversion -Waggregate-return \
	-Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs

SRC = auction.c auctionfile.c auctioninfo.c buffer.c esniper.c http.c \
	options.c util.c
OBJ = auction.o auctionfile.o auctioninfo.o buffer.o esniper.o http.o \
	options.o util.o
HDR = auction.h auctionfile.h auctioninfo.h buffer.h esniper.h http.h \
	options.h util.h

all: esniper

esniper: $(OBJ)
	$(CC) $(CFLAGS) -o esniper $(OBJ) $(LIBS)

$(OBJ): $(HDR)

install: esniper
	$(INSTALL) -s esniper $(INSTALL_DIR)

# Simple portability check - look for no warnings
check:
	gcc -c $(CFLAGS) $(CHECKFLAGS) $(SRC)

clean:
	rm -f esniper $(OBJ) esniper.log.* core
