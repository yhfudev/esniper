#! /bin/sh
set -x

#How to build a release:
#
#From checked-out source directory...
#
#First, setup environment variables:

CVS_RSH=ssh; export CVS_RSH
CVSROOT=:ext:esniper@esniper.cvs.sourceforge.net:/cvsroot/esniper
export CVSROOT
# change these to whatever is current
MAJOR=2
MINOR=21
STEP=0
PREV=2.20.0
# these don't change
CURRENT=$MAJOR.$MINOR.$STEP
CURRFILE=esniper-$MAJOR-$MINOR-$STEP
CURRTAG=Version_${MAJOR}_${MINOR}_${STEP}

#Ensure the version is correct in configure.in, rebuild the automake files,
#and check them in the correct order so the dependencies are correct:

#vi configure.in
perl -i.bak -p -e 's/(AM_INIT_AUTOMAKE\(esniper),.*\)/\1,'${CURRENT}')/' configure.in

make
cvs ci -m $CURRENT configure.in
sleep 2
cvs ci -f -m $CURRENT aclocal.m4 Makefile.am
sleep 2
cvs ci -f -m $CURRENT configure Makefile.in
rm configure.in aclocal.m4 Makefile.am configure Makefile.in
cvs update configure.in aclocal.m4 Makefile.am configure Makefile.in

#Tag source:

cvs tag -F $CURRTAG

#Create a directory:

mkdir $CURRENT

#Copy Changes and Notes from the previous build:

cp ChangeLog $CURRENT/Changes
cp $PREV/Notes $PREV/index.html $CURRENT
echo $CURRENT >$CURRENT/version.txt

#Create source tar:

cd $CURRENT
mkdir $CURRFILE $CURRFILE/frontends
cp ../auction.c ../auction.h $CURRFILE
cp ../auctionfile.c ../auctionfile.h $CURRFILE
cp ../auctioninfo.c ../auctioninfo.h $CURRFILE
cp ../buffer.c ../buffer.h $CURRFILE
cp ../esniper.c ../esniper.h $CURRFILE
cp ../history.c ../history.h $CURRFILE
cp ../html.c ../html.h $CURRFILE
cp ../http.c ../http.h $CURRFILE
cp ../options.c ../options.h $CURRFILE
cp ../util.c ../util.h $CURRFILE
cp ../aclocal.m4 ../configure ../configure.in ../depcomp $CURRFILE
cp ../install-sh ../Makefile.am ../Makefile.in ../missing $CURRFILE
cp ../AUTHORS ../COPYING ../INSTALL ../NEWS ../README ../TODO $CURRFILE
cp ../ChangeLog $CURRFILE
cp ../esniper.1 ../esniper_man.html ../misc.mk $CURRFILE
cp ../sample_auction.txt ../sample_config.txt $CURRFILE
cp ../frontends/README ../frontends/snipe $CURRFILE/frontends
tar cvf - $CURRFILE | gzip >$CURRFILE.tgz

#Edit Changes and Notes:

#vi Changes Notes index.html

#(Delete changes for prior releases.
# Change version # in Notes and index.html)

#Copy files to sourceforge:
#
#	sftp esniper@frs.sourceforge.net
#		cd uploads
#		mput *.tgz
#		quit
#
#Go to www.sourceforge.net:
#	- login
#	- go to project
#	- click on admin, then file releases, then add release
#	- type in release number and click "create this release"
#	- cut/paste notes and changes, ensure "preserve text" is selected
#	- select file(s) to import
#	- mark each type (one at a time...).  Source .tgz file should be
#	  processor "platform independent" and file type "Source .gz".
#	- click "send notice"
#	- click file releases, then edit releases, then find the previous
#	  release and edit it.  Change the status to hidden and submit.
#
#Copy other files to sourceforge:
#
#	scp $CURRFILE/sample* $CURRFILE/esniper.1 $CURRFILE/*.html \
#		version.txt index.html \
#		esniper@shell.sf.net:/home/groups/e/es/esniper/htdocs
#
#Send email to esniper-announce@lists.sourceforge.net to announce the
#new version.
#
#And you're done...
