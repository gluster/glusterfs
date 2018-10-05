#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

SHARD_COUNT_TIME=5

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST dd if=/dev/zero conv=fsync of=$M0/one-plus-five-shards bs=1M count=23

ACTIVE_INODES_BEFORE=$(get_mount_active_size_value $V0)
TEST rm -f $M0/one-plus-five-shards
# Expect 5 inodes less. But one inode more than before because .remove_me would be created.
EXPECT_WITHIN $SHARD_COUNT_TIME `expr $ACTIVE_INODES_BEFORE - 5 + 1` get_mount_active_size_value $V0 $M0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
