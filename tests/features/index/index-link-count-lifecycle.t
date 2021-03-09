#!/bin/bash
#Index link-count lifecycle tests

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST $CLI volume heal $V0 disable
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#When the bricks are started link-count should be zero if no heals are needed
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick0 "xattrop-pending-count"
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick1 "xattrop-pending-count"
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick2 "xattrop-pending-count"

#No index file should be created when op succeeds on all bricks
echo abc > $M0/abc
TEST rm -f $M0/abc
EXPECT "^0$" count_index_entries  $B0/brick0
EXPECT "^0$" count_index_entries  $B0/brick1
EXPECT "^0$" count_index_entries  $B0/brick2
#When heal is needed xattrop-pending-count should reflect number of files to be healed
TEST kill_brick $V0 $H0 $B0/brick0
echo abc > $M0/a
TEST ls $M0 #Perform a lookup to make sure the values are updated
EXPECT "^2$" get_value_from_brick_statedump $V0 $H0 $B0/brick1 "xattrop-pending-count"
EXPECT "^2$" get_value_from_brick_statedump $V0 $H0 $B0/brick2 "xattrop-pending-count"

#Once heals are completed pending count should be back to zero
TEST $CLI volume heal $V0 enable
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick0 "xattrop-pending-count"
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick1 "xattrop-pending-count"
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick2 "xattrop-pending-count"

cleanup;

#Same tests for EC volume, EC doesn't fetch link-count, so it is not refreshed
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 $H0:$B0/brick{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume heal $V0 disable
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#When the bricks are started link-count should be zero if no heals are needed
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick0 "xattrop-pending-count"
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick1 "xattrop-pending-count"
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick2 "xattrop-pending-count"

#When heal is needed xattrop-pending-count should reflect number of files to be healed
TEST kill_brick $V0 $H0 $B0/brick0
echo abc > $M0/a
TEST ls $M0 #EC doesn't request link-count, so the values will stay '-1'
EXPECT "^-1$" get_value_from_brick_statedump $V0 $H0 $B0/brick1 "xattrop-pending-count"
EXPECT "^-1$" get_value_from_brick_statedump $V0 $H0 $B0/brick2 "xattrop-pending-count"

#Once heals are completed pending count should be back to zero
TEST $CLI volume heal $V0 enable
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count_shd $V0 0
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick0 "xattrop-pending-count"
#pending-count is never requested on disperse so it will be stuck at -1(i.e. cache is invalidated) after heal completes
EXPECT "^-1$" get_value_from_brick_statedump $V0 $H0 $B0/brick1 "xattrop-pending-count"
EXPECT "^-1$" get_value_from_brick_statedump $V0 $H0 $B0/brick2 "xattrop-pending-count"
cleanup;

#Same tests for distribute volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#When the brick is started link-count should be zero
EXPECT "^0$" get_value_from_brick_statedump $V0 $H0 $B0/brick0 "xattrop-pending-count"

cleanup;
