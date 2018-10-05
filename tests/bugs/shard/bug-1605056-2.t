#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 features.shard-lru-limit 25
TEST $CLI volume set $V0 performance.write-behind off

TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Perform a write that would cause 25 shards to be created under .shard
TEST dd if=/dev/zero of=$M0/foo bs=1M count=104

# Write into another file bar to ensure all of foo's shards are evicted from lru list of $M0
TEST dd if=/dev/zero of=$M0/bar bs=1M count=104

# Delete foo from $M0. If there's a bug, the mount will crash.
TEST unlink $M0/foo

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
