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
TEST glusterd -LDEBUG
TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume create $V1 $H0:$B0/brick{2,3}

TEST $CLI volume set all cluster.brick-multiplex on

TEST $CLI volume start $V0
TEST $CLI volume start $V1

EXPECT 1 count_brick_processes

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST rm -rf $H0:$B0/brick{0,1}

#Check No. of brick processes after remove brick from back-end
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V1

EXPECT 1 count_brick_processes

TEST glusterfs -s $H0 --volfile-id $V1 $M0
TEST touch $M0/file{1..10}

pkill glusterd
TEST glusterd -LDEBUG
sleep 5
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks $V1


cleanup

