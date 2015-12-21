#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST touch $M0/dir/file

# Create "file" with holes.
TEST truncate -s 6M $M0/dir/file
EXPECT '6291456' stat -c %s $M0/dir/file

# Perform writes that do not cross the 6M boundary
TEST dd if=/dev/zero of=$M0/dir/file bs=1024 seek=3072 count=2048 conv=notrunc

# Ensure that the file size is 6M (as opposed to 8M that would appear in the
# presence of this bug).
EXPECT '6291456' stat -c %s $M0/dir/file

#Extend the write beyond EOF such that it again creates a hole of 1M size
TEST dd if=/dev/zero of=$M0/dir/file bs=1024 seek=7168 count=2048 conv=notrunc

# Ensure that the file size is not greater than 9M.
EXPECT '9437184' stat -c %s $M0/dir/file
cleanup
