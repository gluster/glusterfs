#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

cleanup;

TEST launch_cluster 2
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

#bug-1549996 - stale brick processes on the nodes after volume deletion

TEST $CLI_1 volume set all cluster.brick-multiplex on
TEST $CLI_1 volume create $V0 replica 3 $H1:$B1/${V0}{1..3} $H2:$B2/${V0}{1..3}
TEST $CLI_1 volume start $V0

TEST $CLI_1 volume create $V1 replica 3 $H1:$B1/${V1}{1..3} $H2:$B2/${V1}{1..3}
TEST $CLI_1 volume start $V1

EXPECT 2 count_brick_processes

TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume stop $V1

EXPECT 0 count_brick_processes

cleanup

