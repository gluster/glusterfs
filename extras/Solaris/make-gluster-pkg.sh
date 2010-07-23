#!/usr/bin/bash
#
# Copyright (C) 2010 Gluster Inc.
#
# script to build GlusterFS Packages for Solaris
#

export PATH=/opt/csw/bin:/opt/csw/gcc4/bin:/usr/ccs/bin:/usr/ucb:/usr/sfw/bin:$PATH

INSTALL_BASE=/opt/glusterfs

VERSION=3.0.5
SBINPROS="glusterfsd glusterfs"
BINPROGS="glusterfs-volgen"
#####################################################################
## BEGIN MAIN 
#####################################################################

TMPINSTALLDIR=/tmp/build

# Try to guess the distribution base..
CURR_DIR=`pwd`
echo "Assuming GlusterFS distribution is rooted at $CURR_DIR .."

##
## first build the source
##

WGET=`which wget`
$WGET http://ftp.gluster.com/pub/gluster/glusterfs/3.0/$VERSION/glusterfs-$VERSION.tar.gz

tar xf glusterfs-$VERSION.tar.gz 
mv glusterfs-$VERSION source

if [ "x$1" != "xnobuild" ]; then

	./configure.sh

	if [ $? -ne 0 ]; then
		echo "Build failed!  Exiting...."
		exit 1
	fi
fi
	
cd $CURR_DIR/source
make DESTDIR=$TMPINSTALLDIR install

cd $CURR_DIR
##
## Now set the install locations
##
SBINDIR=/opt/glusterfs/sbin
BINDIR=/opt/glusterfs/bin
CONFIGDIR=/opt/glusterfs/etc/glusterfs
##
## Main driver 
##
## copy over some scripts need for packagaing
##
mkdir -p $TMPINSTALLDIR/etc/init.d
	cp -fp glusterfsd $TMPINSTALLDIR/etc/init.d
mkdir -p $TMPINSTALLDIR/etc/rc3.d
        cp -fp glusterfsd $TMPINSTALLDIR/etc/rc3.d/S52glusterfsd
mkdir -p $CONFIGDIR
	cp -fp options $TMPINSTALLDIR$CONFIGDIR

##
## Start building the prototype file
##
cp pkginfo.master pkginfo

echo "SBINDIR=$SBINDIR" >> pkginfo
echo "BINDIR=$BINDIR" >> pkginfo
echo "CONFIGDIR=$CONFIGDIR" >> pkginfo

##
## copy packaging files 
##
for i in pkginfo copyright preremove postinstall request checkinstall; do
	cp $i /
done


cd /
(echo 'i pkginfo'; echo 'i copyright'; echo 'i preremove'; echo 'i postinstall'; echo 'i request'; pkgproto /$TMPINSTALLDIR=/ ) >prototype

GREP=`which ggrep`
$GREP -w '/' /prototype -v > /prototype.new

mv /prototype.new /prototype

# Create the package
pkgmk -o -d /tmp -f prototype

if [ $? = 0 ]; then
	pkgtrans /tmp glusterfs_${VERSION}_i386.pkg glusterfs
fi

echo "Cleaning up build files"

rm -rf $TMPINSTALLDIR

for i in pkginfo copyright preremove postinstall request checkinstall; do
        rm /$i
done

echo The GlusterFS package is in /tmp
