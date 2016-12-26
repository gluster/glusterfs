#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup

TESTS_EXPECTED_IN_LOOP=12

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume heal $V0 granular-entry-heal enable
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 performance.flush-behind off

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M1

cd $M0
TEST dd if=/dev/zero of=file bs=1M count=8

# Kill brick-0.
TEST kill_brick $V0 $H0 $B0/${V0}0

TEST "dd if=/dev/zero bs=1M count=8 >> file"

FILE_GFID=$(get_gfid_string $M0/file)

# Test that the index associated with '/.shard' is created on B1 and B2.
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID
TEST stat $B0/${V0}2/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID
# Check for successful creation of granular entry indices
for i in {2..3}
do
        TEST_IN_LOOP stat $B0/${V0}1/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID/$FILE_GFID.$i
        TEST_IN_LOOP stat $B0/${V0}2/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID/$FILE_GFID.$i
done

cd ~
TEST md5sum $M1/file

# Test that the index associated with '/.shard' and the created shards do not disappear on B1 and B2.
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID
TEST stat $B0/${V0}2/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID
for i in {2..3}
do
        TEST_IN_LOOP stat $B0/${V0}1/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID/$FILE_GFID.$i
        TEST_IN_LOOP stat $B0/${V0}2/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID/$FILE_GFID.$i
done

# Start the brick that was down
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

# Enable shd
TEST gluster volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Now verify that there are no name indices left after self-heal
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID
TEST ! stat $B0/${V0}2/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID

for i in {2..3}
do
        TEST_IN_LOOP ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID/$FILE_GFID.$i
        TEST_IN_LOOP ! stat $B0/${V0}2/.glusterfs/indices/entry-changes/$DOT_SHARD_GFID/$FILE_GFID.$i
done

cleanup
