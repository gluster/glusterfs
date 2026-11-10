#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Test for https://github.com/gluster/glusterfs/issues/4734
#
# Replacing a file with an open descriptor via rename() (or rmdir()ing a
# directory with an open descriptor) must not break fchmod()/fchown()/
# futimens()/fstat() on the still-open descriptor. On a local filesystem
# these operations work because the fd pins the inode; GlusterFS must
# behave the same.

# Create, start and mount the volume.
TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0;
TEST glusterfs -s $H0 --volfile-id $V0 $M0

# Compile the test program.
TEST $CC -Wall $(dirname $0)/issue-4734.c -o $(dirname $0)/issue-4734

# Create test directory.
mkdir -p $M0/dir-issue-4734

# Invoke the test program and pass the directory path to it.
TEST $(dirname $0)/issue-4734 $M0/dir-issue-4734

# Unmount, stop and delete the volume
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
