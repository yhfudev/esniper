# The following line needs to be uncommented if compiling on solaris
#LIBS=-lsocket -lnsl

# install is typically in /usr/bin or /usr/sbin
#INSTALL=/usr/bin/install
#INSTALL=/usr/sbin/install
INSTALL=install

# Change this if you want esniper installed elsewhere
INSTALL_DIR=/usr/local/bin

SRC = auction.c auctionfile.c auctioninfo.c buffer.c esniper.c options.c util.c
OBJ = auction.o auctionfile.o auctioninfo.o buffer.o esniper.o options.o util.o
HDR = auction.h auctionfile.h auctioninfo.h buffer.h esniper.h options.h util.h

all: esniper

esniper: $(OBJ)
	$(CC) -O -o esniper $(OBJ) $(LIBS)

$(OBJ): $(HDR)

install: esniper
	$(INSTALL) -s esniper $(INSTALL_DIR)

# Simple portability check - look for no warnings
check:
	gcc -c -pedantic -Wall $(SRC)

clean:
	rm -f esniper $(OBJ) esniper.log.* core
