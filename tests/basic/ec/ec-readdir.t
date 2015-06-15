#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks that readdir works fine on distributed disperse volume

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..5}
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 1

TEST mkdir $M0/d
TEST touch $M0/d/{1..100}
EXPECT "100" echo $(ls $M0/d/* | wc -l)
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST rm -rf $M0/d/{1..100}
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 1
#Do it 3 times so that even with all load balancing, readdir never falls
#on stale bricks
EXPECT "0" echo $(ls $M0/d/ | wc -l)
EXPECT "0" echo $(ls $M0/d/ | wc -l)
EXPECT "0" echo $(ls $M0/d/ | wc -l)
#Do the same test above for creation of entries
TEST mkdir $M0/d1
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST touch $M0/d1/{1..100}
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 1
#Do it 3 times so that even with all load balancing, readdir never falls
#on stale bricks
EXPECT "100" echo $(ls $M0/d1/ | wc -l)
EXPECT "100" echo $(ls $M0/d1/ | wc -l)
EXPECT "100" echo $(ls $M0/d1/ | wc -l)
cleanup
