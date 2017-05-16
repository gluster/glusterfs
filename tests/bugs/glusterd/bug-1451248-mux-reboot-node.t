#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../volume.rc

function count_up_bricks {
        $CLI --xml volume status all | grep '<status>1' | wc -l
}

function count_brick_processes {
	pgrep glusterfsd | wc -l
}

function count_brick_pids {
        $CLI --xml volume status all | sed -n '/.*<pid>\([^<]*\).*/s//\1/p' \
                                     | grep -v "N/A" | sort | uniq | wc -l
}

cleanup;

TEST glusterd
TEST $CLI volume set all cluster.brick-multiplex on
push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

TEST $CLI volume create $V0 $H0:$B0/brick{0..2}
TEST $CLI volume start $V0

EXPECT 1 count_brick_processes
EXPECT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_up_bricks

pkill gluster
TEST glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_up_bricks

pkill glusterd
TEST glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_up_bricks

TEST $CLI volume create $V1 $H0:$B0/brick{3..5}
TEST $CLI volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 count_up_bricks

