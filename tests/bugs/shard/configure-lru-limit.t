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
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Perform a write that would cause 25 shards to be created, 24 of them under .shard
TEST dd if=/dev/zero of=$M0/foo bs=1M count=100

statedump=$(generate_mount_statedump $V0)
sleep 1
EXPECT "25" echo $(grep "lru-max-limit" $statedump | cut -f2 -d'=' | tail -1)

# Base shard is never added to this list. So all other shards should make up for 24 inodes in lru list
EXPECT "24" echo $(grep "inode-count" $statedump | cut -f2 -d'=' | tail -1)

rm -f $statedump

# Test to ensure there's no "reconfiguration" of the value once set.
TEST $CLI volume set $V0 features.shard-lru-limit 30
statedump=$(generate_mount_statedump $V0)
sleep 1
EXPECT "25" echo $(grep "lru-max-limit" $statedump | cut -f2 -d'=' | tail -1)
rm -f $statedump

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
statedump=$(generate_mount_statedump $V0)
sleep 1
EXPECT "30" echo $(grep "lru-max-limit" $statedump | cut -f2 -d'=' | tail -1)
rm -f $statedump

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
