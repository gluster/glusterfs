#!/bin/bash
#Tests that afr up/down works as expected

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,3,4,5,6}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
EXPECT "1" afr_up_status $V0 $M0 0
EXPECT "1" afr_up_status $V0 $M0 1

#kill two bricks in first replica and check that afr_up_status is 0 for it
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" afr_up_status $V0 $M0 0
EXPECT "1" afr_up_status $V0 $M0 1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_up_status $V0 $M0 0
EXPECT "1" afr_up_status $V0 $M0 1
cleanup;
