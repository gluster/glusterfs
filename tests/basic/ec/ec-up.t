#!/bin/bash
#Tests that ec subvolume is up/down correctly

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../ec.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse-data 2 redundancy 1 $H0:$B0/${V0}{0,1,3,4,5,6}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
EXPECT "1" ec_up_status $V0 $M0 0
EXPECT "1" ec_up_status $V0 $M0 1

#kill two bricks in first disperse subvolume and check that ec_up_status is 0 for it
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" ec_up_status $V0 $M0 0
EXPECT "1" ec_up_status $V0 $M0 1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ec_up_status $V0 $M0 0
EXPECT "1" ec_up_status $V0 $M0 1
cleanup;
