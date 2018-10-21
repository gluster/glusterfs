#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

# Test to check that data self-heal does not leave any stale lock.

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

# Create base entry in indices/xattrop
echo "Data" > $M0/FILE

# Kill arbiter brick and write to FILE.
TEST kill_brick $V0 $H0 $B0/${V0}2
echo "arbiter down" >> $M0/FILE
EXPECT 2 get_pending_heal_count $V0

# Bring it back up and let heal complete.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# write to the FILE must succeed.
echo "this must succeed" >> $M0/FILE
TEST [ $? -eq 0 ]
cleanup;
