#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

function get_cumulative_opendir_count {
#sed command prints content between Cumulative and Interval, this keeps content from Cumulative stats
    $CLI volume profile $V0 info |sed -n '/^Cumulative/,/^Interval/p'|grep OPENDIR| awk '{print $8}'|tr -d '\n'
}

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
# Each brick does 4 opendirs, corresponding to dirty, xattrop and entry-changes, anonymous-inode
EXPECT_WITHIN 4 "^444$" get_cumulative_opendir_count

# Check that a change in heal-timeout is honoured immediately.
TEST $CLI volume set $V0 cluster.heal-timeout 5
sleep 10
# Two crawls must have happened.
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^121212$" get_cumulative_opendir_count

# shd must not heal if it is disabled and heal-timeout is changed.
TEST $CLI volume heal $V0 disable
#Wait for configuration update and any opendir fops to complete
sleep 10
TEST $CLI volume profile $V0 info clear
TEST $CLI volume set $V0 cluster.heal-timeout 6
#Better to wait for more than 6 seconds to account for configuration updates
sleep 10
COUNT=`$CLI volume profile $V0 info incremental |grep OPENDIR|awk '{print $8}'|tr -d '\n'`
TEST [ -z $COUNT ]
cleanup;
