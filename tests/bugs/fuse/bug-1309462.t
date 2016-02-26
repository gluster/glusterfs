#!/bin/bash
#
# https://bugzilla.redhat.com/show_bug.cgi?id=1309462
# Test the new fuse mount option --capability.
# Set/get xattr on security.capability should be sent
# down from fuse, only if --selinux or --capability option
# is used for mounting.

. $(dirname $0)/../../include.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE without selinux:
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TESTFILE="$M0/testfile"
TEST touch ${TESTFILE}

TEST ! setfattr -n security.capability -v value ${TESTFILE}
TEST ! getfattr -n security.capability ${TESTFILE}

TEST umount $M0

# Mount FUSE with selinux:
TEST glusterfs -s $H0 --volfile-id $V0 --selinux $M0

TEST setfattr -n security.capability -v value ${TESTFILE}
TEST getfattr -n security.capability ${TESTFILE}
TEST setfattr -x security.capability ${TESTFILE}

TEST umount $M0

# Mount FUSE with capability:
TEST glusterfs -s $H0 --volfile-id $V0 --capability $M0

TEST setfattr -n security.capability -v value ${TESTFILE}
TEST getfattr -n security.capability ${TESTFILE}
TEST setfattr -x security.capability ${TESTFILE}

TEST umount $M0

