#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST touch $M0/dir/file_plain

# Create "file_plain" with holes.
TEST truncate -s 12M $M0/dir/file_plain

# Perform writes on it that would create holes.
TEST dd if=/dev/zero of=$M0/dir/file_plain bs=1024 seek=10240 count=1024 conv=notrunc

md5sum_file_plain=$(md5sum $M0/dir/file_plain | awk '{print $1}')

# Now enable sharding on the volume.
TEST $CLI volume set $V0 features.shard on

# Create a sharded file called "file_sharded"
TEST touch $M0/dir/file_sharded

# Truncate it to make it sparse
TEST truncate -s 12M $M0/dir/file_sharded

# Perform writes on it that would create holes in block-0 and block-1.
TEST dd if=/dev/zero of=$M0/dir/file_sharded bs=1024 seek=10240 count=1024 conv=notrunc

# If this bug is fixed, md5sum of file_sharded and file_plain should be same.
EXPECT "$md5sum_file_plain" echo `md5sum $M0/dir/file_sharded | awk '{print $1}'`

cleanup
