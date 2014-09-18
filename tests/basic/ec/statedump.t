#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 1 $H0:$B0/${V0}{0..2}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_status $V0 0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_status $V0 0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_status $V0 0 2

TEST kill_brick $V0 $H0 $B0/${V0}0

EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "0" ec_child_up_status $V0 0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_status $V0 0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_status $V0 0 2

cleanup
