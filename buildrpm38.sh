#!/bin/bash
#
# Simple script to clean-house and build some RPMs
#

RPMBUILD_BIN='/usr/bin/rpmbuild'

function usage {
cat << EOF

Usage $0 <release tag> <extra rpm build flags>

e.g. "$0 4" builds RPMs with a version of 3.8_fb-4.

e.g. "$0 4 --with asan" builds RPMS with a version of 3.8_fb-4 with ASAN turned on

EOF
exit 1
}

(( $# == 0 )) && usage

echo -n "Stashing uncommitted files..."
if STASH_OUTPUT=$(git stash); then
  if echo $STASH_OUTPUT | grep -q "No local changes"; then
    echo "No changes found"
  else
    # Make sure we clean up even if someone exits early on failure.
    trap "git stash pop" EXIT
    echo DONE
  fi
else
  echo "Failed to stash uncommitted files, aborting!" && exit 1
fi

RELEASE_TAG=$1
echo -n "Updating glusterfs.spec.in file..."
if sed -i "s@%global release fb_release@%global release $RELEASE_TAG@g" glusterfs.spec.in; then
  echo DONE
else
  echo FAILED && exit 1
fi

EXTRA_RPM_BUILD_FLAGS=${@:2}

# We need to patch find-debug-info.sh to prevent symbol stripping
# while still building a debuginfo RPM which contains our source.
# This makes debugging MUCH easier.  This patch works for both
# CentOS 5 & 6

# Don't sudo for svcscm user as this will break jenkins
[ $USER == "svcscm" ] || sudo ./patch-find-debuginfo.sh

echo -n "Checking for .rpmmacros...."
if grep -q "%_topdir" ~/.rpmmacros; then
  echo DONE
else
  echo "not found"
  echo "Adding _topdir to .rpmmacros..."
  echo "%_topdir /home/$USER/local/rpmbuild" >> ~/.rpmmacros
fi

echo -n "Checking for ~/local/rpmbuild directory..."
if [ -d ~/local/rpmbuild ]; then
  echo DONE
else
  echo "not found"
  echo "Creating rpmbuild directories..."
  mkdir -vp ~/local/rpmbuild/BUILD
  mkdir -vp ~/local/rpmbuild/BUILDROOT
  mkdir -vp ~/local/rpmbuild/RPMS
  mkdir -vp ~/local/rpmbuild/SOURCES
  mkdir -vp ~/local/rpmbuild/SPECS
  mkdir -vp ~/local/rpmbuild/SRPMS
fi

echo "Building GlusterFS..."
source ./build_env
./build.sh

echo "Creating tarball for rpmbuild..."
make -j dist
echo -n "Restoring glusterfs.spec.in..."
git checkout glusterfs.spec.in &> /dev/null
echo DONE

MY_TARBALL=~/local/rpmbuild/glusterfs-3.8.15_fb.tar.gz
cp $(basename $MY_TARBALL) $MY_TARBALL
MY_RPM_BUILD_FLAGS="--with fbextras --without georeplication"
ALL_RPM_BUILD_FLAGS="$MY_RPM_BUILD_FLAGS $EXTRA_RPM_BUILD_FLAGS"
if ! $RPMBUILD_BIN -tb $MY_TARBALL $ALL_RPM_BUILD_FLAGS; then
  exit 1
fi

exit 0
