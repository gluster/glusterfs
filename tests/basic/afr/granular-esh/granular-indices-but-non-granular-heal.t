#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../afr.rc

cleanup

TESTS_EXPECTED_IN_LOOP=4

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume heal $V0 granular-entry-heal enable

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

# Kill brick-0.
TEST kill_brick $V0 $H0 $B0/${V0}0

# Create files under root
for i in {1..2}
do
        echo $i > $M0/f$i
done

# Test that the index associated with '/' is created on B1.
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID

# Check for successful creation of granular entry indices
for i in {1..2}
do
        TEST_IN_LOOP stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f$i
done

# Now disable granular-entry-heal
TEST $CLI volume heal $V0 granular-entry-heal disable

# Start the brick that was down
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

# Enable shd
TEST gluster volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

# Now the indices created are granular but the heal is going to be of the
# normal kind. We test to make sure that heal still completes fine and that
# the stale granular indices are going to be deleted

TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Test if data was healed
for i in {1..2}
do
        TEST_IN_LOOP diff $B0/${V0}0/f$i $B0/${V0}1/f$i
done

# Now verify that there are no name indices left after self-heal
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID

cleanup
