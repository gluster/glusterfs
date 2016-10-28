#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc


cleanup

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/b1 $H1:$B1/b2 $H2:$B2/b3
TEST $CLI_1 volume start $V0

#Start a fix-layout
TEST $CLI_1 volume rebalance $V0 fix-layout start

#volume rebalance status should work
TEST $CLI_1 volume rebalance $V0 status
$CLI_1 volume rebalance $V0 status

val=$($CLI_1 volume rebalance $V0 status |grep "fix-layout" 2>&1)
val=$?
TEST [ $val -eq 0 ];

#Start a remove brick for the brick on H2
TEST $CLI_1 volume remove-brick $V0 $H2:$B2/b3 start
TEST $CLI_1 volume remove-brick $V0 $H2:$B2/b3 status

#Check remove brick status from H1
$CLI_1 volume remove-brick $V0 $H2:$B2/b3 status |grep "fix-layout" 2>&1
val=$?
TEST [ $val -eq 1 ];

$CLI_1 volume remove-brick $V0 $H2:$B2/b3 status
$CLI_2 volume remove-brick $V0 $H2:$B2/b3 status


TEST $CLI_1 volume remove-brick $V0 $H2:$B2/b3 stop

cleanup
