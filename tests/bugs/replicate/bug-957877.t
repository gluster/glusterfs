#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;
kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/f1
TEST setfattr -n "user.foo" -v "test" $M0/f1

BRICK=$B0"/${V0}1"

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

# Wait for self-heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT '0' count_sh_entries $BRICK;

TEST getfattr -n "user.foo" $B0/${V0}0/f1;

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
