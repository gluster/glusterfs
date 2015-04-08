#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks that readdir works fine on distributed disperse volume

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 1

TEST touch $M0/{1..100}
EXPECT "100" echo $(ls $M0 | wc -l)
cleanup
