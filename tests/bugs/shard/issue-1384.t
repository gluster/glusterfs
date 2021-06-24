#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST $CLI volume set $V0 md-cache-timeout 10

# Write data into a file such that its size crosses shard-block-size
TEST dd if=/dev/zero of=$M0/foo bs=1048576 count=10 oflag=direct

# Size of the file should be the aggregated size, not the shard-block-size
EXPECT "10485760" echo `ls -la $M0 | grep foo | awk '{print $5}'`

TEST $CLI volume set $V0 performance.read-ahead on
TEST $CLI volume set $V0 performance.parallel-readdir on

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Size of the file should be the aggregated size, not the shard-block-size
EXPECT "10485760" echo `ls -la $M0 | grep foo | awk '{print $5}'`

cleanup
