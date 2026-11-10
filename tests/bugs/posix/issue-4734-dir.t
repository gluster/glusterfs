#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Test for the directory part of https://github.com/gluster/glusterfs/issues/4734
#
# rmdir() of a directory that has an open descriptor must remove it from
# the namespace while keeping the fd-based metadata operations
# (fchmod/futimens/fsetxattr/fgetxattr) working on that descriptor, and
# rename() replacing an open empty directory must behave the same, exactly
# like on a local filesystem.

TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0;
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST $CC -Wall $(dirname $0)/issue-4734-dir.c -o $(dirname $0)/issue-4734-dir

mkdir -p $M0/dir-issue-4734-dir

# Invoke the test program and pass the directory path to it.
TEST $(dirname $0)/issue-4734-dir $M0/dir-issue-4734-dir

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
