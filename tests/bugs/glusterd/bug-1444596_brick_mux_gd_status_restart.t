#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc


function count_up_bricks {
        $CLI --xml volume status $1 | grep '<status>1' | wc -l
}

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

cleanup
TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume create $V1 $H0:$B0/brick{2,3}

TEST $CLI volume set all cluster.brick-multiplex on

TEST $CLI volume start $V0
TEST $CLI volume start $V1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V1
EXPECT 1 count_brick_processes

pkill glusterd
TEST glusterd

#Check brick status after restart glusterd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V1


TEST $CLI volume stop $V0
TEST $CLI volume stop $V1

cleanup

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume create $V1 $H0:$B0/brick{2,3}

TEST $CLI volume set all cluster.brick-multiplex on

TEST $CLI volume start $V0
TEST $CLI volume start $V1

EXPECT 1 count_brick_processes

TEST $CLI volume set $V0 performance.cache-size 32MB
TEST $CLI volume stop $V0
TEST $CLI volume start $V0

#Check No. of brick processes after change option
EXPECT 2 count_brick_processes

pkill glusterd
TEST glusterd

#Check brick status after restart glusterd should not be NA
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V1
EXPECT 2 count_brick_processes

cleanup
