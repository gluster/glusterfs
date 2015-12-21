#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

# $M0 is where the reads will be done and $M1 is where files will be created,
# written to, etc.
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M1

# Write some data into a file, such that its size crosses the shard block size.
TEST dd if=/dev/zero of=$M1/file bs=1M count=5 conv=notrunc

md5sum1_reader=$(md5sum $M0/file | awk '{print $1}')

EXPECT "$md5sum1_reader" echo `md5sum $M1/file | awk '{print $1}'`

# Append some more data into the file.
TEST `echo "abcdefg" >> $M1/file`

md5sum2_reader=$(md5sum $M0/file | awk '{print $1}')

# Test to see if the reader refreshes its cache correctly as part of the reads
# triggered through md5sum. If it does, then the md5sum on the reader and writer
# must match.
EXPECT "$md5sum2_reader" echo `md5sum $M1/file | awk '{print $1}'`

cleanup
