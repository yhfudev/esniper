#! /bin/sh

if [ $# -ne 3 ]; then
	echo ReleaseBuild.sh requires three arguments, which are major, minor and step
	echo versions. For example, to build version 2.15.6 you would give 2 15 6 as arguemnts.
	exit 2
fi

echo "If you have not updated and committed ChangeLog, please quit this script"
echo "by pressing CTRL+C and do it now!"
echo "Press enter to continue."
read line

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

echo Creating ReleaseNote and README file from ChangeLog.

awk 'BEGIN {empty=0;stop=0;buffer="";}
/^[     ]*$/ {
  empty=1;
  if(!stop && length(buffer)) printf "%s", buffer;
  buffer = "";
}
{ if(!stop) {
    if(!empty) print;
    else {
      buffer = buffer $0 "\n";
      if(match($0, /\* [0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]* released/)) stop=1;
    }
  }
}' ChangeLog > ReleaseNote
cp ReleaseNote README

echo Please check the contents of the ReleaseNote file. It should contain
echo all changes for the current version but not the history as in ChangeLog.
echo "--- start ---"
cat ReleaseNote
echo "---  end  ---"
echo "If the ReleaseNote file is not OK or if you are not sure, press CTRL+C"
echo "to stop this script now and fix ChangeLog or this script."
echo "Press enter to continue."
read line

exit

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

# The perl command did not work. Replaced by download link to latest version.
#perl -i.bak -p -e 's/esniper-.*[.]tgz/'${CURRENT}'.tgz/' index.html

cvs ci -m $CURRENT version.txt ReleaseNote README index.html


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
