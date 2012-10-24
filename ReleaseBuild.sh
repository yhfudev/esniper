#! /bin/sh

echo "If you have not updated ReleaseNote, please quit this script and do it now!"
echo "Hit enter to continue."
read line

if [ $# -ne 3 ]; then
	echo ReleaseBuild.sh requires three arguments, which are major, minor and step
	echo versions. For example, to build version 2.15.6 you would give 2 15 6 as arguemnts.
	exit 2
fi

# setup environment variables:

MAJOR="$1"
MINOR="$2"
STEP="$3"
CURRENT=$MAJOR.$MINOR.$STEP
CURRFILE=esniper-$MAJOR-$MINOR-$STEP
CURRTAG=Version_${MAJOR}_${MINOR}_${STEP}

CVS_RSH=ssh; export CVS_RSH
CVSROOT=:ext:esniper@esniper.cvs.sourceforge.net:/cvsroot/esniper
export CVSROOT

echo Rebuilding automake files with current version number.  CVS will be called four
echo times. You will see some "lost" errors, in the last CVS command.  That is OK.

perl -i.bak -p -e 's/(AM_INIT_AUTOMAKE\(esniper),.*\)/\1,'${CURRENT}')/' configure.in
make
cvs ci -m $CURRENT configure.in
sleep 2
cvs ci -f -m $CURRENT aclocal.m4 Makefile.am
sleep 2
cvs ci -f -m $CURRENT configure Makefile.in
rm configure.in aclocal.m4 Makefile.am configure Makefile.in
cvs update configure.in aclocal.m4 Makefile.am configure Makefile.in

echo Modifying version.txt and index.html, then checkin with CVS.

echo $CURRENT >version.txt

perl -i.bak -p -e 's/esniper-.*[.]tgz/'${CURRENT}'.tgz/' index.html

cvs ci -m $CURRENT version.txt ReleaseNote index.html


echo Tagging source.

cvs tag -F $CURRTAG


echo Creating source tar file ${CURRFILE}.tgz

mkdir $CURRFILE $CURRFILE/frontends
cp auction.c auction.h $CURRFILE
cp auctionfile.c auctionfile.h $CURRFILE
cp auctioninfo.c auctioninfo.h $CURRFILE
cp buffer.c buffer.h $CURRFILE
cp esniper.c esniper.h $CURRFILE
cp history.c history.h $CURRFILE
cp html.c html.h $CURRFILE
cp http.c http.h $CURRFILE
cp options.c options.h $CURRFILE
cp util.c util.h $CURRFILE
cp aclocal.m4 configure configure.in depcomp $CURRFILE
cp install-sh Makefile.am Makefile.in missing $CURRFILE
cp AUTHORS COPYING INSTALL NEWS README TODO $CURRFILE
cp ChangeLog $CURRFILE
cp esniper.1 esniper_man.html misc.mk $CURRFILE
cp sample_auction.txt sample_config.txt $CURRFILE
cp frontends/README frontends/snipe $CURRFILE/frontends
tar cvf - $CURRFILE | gzip >$CURRFILE.tgz
