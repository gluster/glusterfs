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
TEST ! $CLI volume set all cluster.max-bricks-per-process -1
TEST ! $CLI volume set all cluster.max-bricks-per-process foobar
TEST $CLI volume set all cluster.max-bricks-per-process 3

push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

TEST $CLI volume create $V0 $H0:$B0/brick{0..5}
TEST $CLI volume start $V0

EXPECT 2 count_brick_processes
EXPECT 2 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 count_up_bricks

pkill gluster
TEST glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 count_up_bricks

TEST $CLI volume add-brick $V0 $H0:$B0/brick6

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 7 count_up_bricks

TEST $CLI volume remove-brick $V0 $H0:$B0/brick3 start
TEST $CLI volume remove-brick $V0 $H0:$B0/brick3 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_brick_processes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 6 count_up_bricks
