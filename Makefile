# The following line needs to be uncommented if compiling on solaris
#LIBS=-lsocket -lnsl

# install is typically in /usr/bin or /usr/sbin
#INSTALL=/usr/bin/install
#INSTALL=/usr/sbin/install
INSTALL=install

# Change this if you want esniper installed elsewhere
INSTALL_DIR=/usr/local/bin

all: esniper

esniper: esniper.o
	$(CC) -O -o esniper esniper.o $(LIBS)

install: esniper
	$(INSTALL) -s esniper $(INSTALL_DIR)

# Simple portability check - look for no warnings
check: esniper.c
	gcc -c -pedantic -Wall esniper.c

clean:
	rm -f esniper esniper.o esniper.log.* core
