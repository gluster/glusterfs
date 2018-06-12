#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../common-utils.rc

# Test overview:
# Check that non-privileged users can also clean up stale linkto files
#
# 1. Use the current parallel-readdir behaviour of changing the DHT child subvols
# in the graph to generate stale linkto files
# 2. Access the file with the stale linkto file as a non-root user
# 3. This should now succeed (returned EIO before commit 3fb1df7870e03c9de)

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0-{1..3}
TEST $CLI volume start $V0

# Mount using FUSE and create a file
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST glusterfs -s $H0 --volfile-id $V0 $M1

ls $M0/FILE-1
EXPECT "2" echo $?


# Create a file and a directory on $M0
TEST dd if=/dev/urandom of=$M0/FILE-1 count=1 bs=16k
TEST mkdir $M0/dir1

ls $M0/FILE-1
EXPECT "0" echo $?

ls $M0/dir1
EXPECT "0" echo $?

#Use a fresh mount so as to trigger a fresh lookup
TEST glusterfs -s $H0 --volfile-id $V0 $M1

TEST ls $M1/FILE-1
EXPECT "0" echo $?


ls $M1/dir1
EXPECT "0" echo $?

# Cleanup
cleanup

