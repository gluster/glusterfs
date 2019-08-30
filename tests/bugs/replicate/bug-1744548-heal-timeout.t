#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST ! $CLI volume heal $V0

# Enable shd and verify that index crawl is triggered immediately.
TEST $CLI volume profile $V0 start
TEST $CLI volume profile $V0 info clear
TEST $CLI volume heal $V0 enable
TEST $CLI volume heal $V0
# Each brick does 3 opendirs, corresponding to dirty, xattrop and entry-changes
COUNT=`$CLI volume profile $V0 info incremental |grep OPENDIR|awk '{print $8}'|tr -d '\n'`
TEST [ "$COUNT" == "333" ]

# Check that a change in heal-timeout is honoured immediately.
TEST $CLI volume set $V0 cluster.heal-timeout 5
sleep 10
COUNT=`$CLI volume profile $V0 info incremental |grep OPENDIR|awk '{print $8}'|tr -d '\n'`
# Two crawls must have happened.
TEST [ "$COUNT" == "666" ]

# shd must not heal if it is disabled and heal-timeout is changed.
TEST $CLI volume heal $V0 disable
TEST $CLI volume profile $V0 info clear
TEST $CLI volume set $V0 cluster.heal-timeout 6
sleep 6
COUNT=`$CLI volume profile $V0 info incremental |grep OPENDIR|awk '{print $8}'|tr -d '\n'`
TEST [ -z $COUNT ]
cleanup;
