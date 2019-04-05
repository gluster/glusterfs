#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 features.shard-lru-limit 120
TEST $CLI volume set $V0 features.shard-deletion-rate 120
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST build_tester $(dirname $0)/bug-1696136.c -lgfapi -Wall -O2

# Create a file
TEST touch $M0/file1

# Fallocate a 500M file. This will make sure number of participant shards are > lru-limit
TEST $(dirname $0)/bug-1696136 $H0 $V0 "0" "0" "536870912" /file1 `gluster --print-logdir`/glfs-$V0.log

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
rm -f $(dirname $0)/bug-1696136

cleanup
