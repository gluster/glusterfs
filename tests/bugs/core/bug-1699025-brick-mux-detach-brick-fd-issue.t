#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

cleanup

#bug-1444596 - validating brick mux

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume create $V1 $H0:$B0/brick{2,3}

TEST $CLI volume set all cluster.brick-multiplex on

TEST $CLI volume start $V0
TEST $CLI volume start $V1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 online_brick_count
EXPECT 1 count_brick_processes

TEST $CLI volume stop $V1
# At the time initialize brick daemon it always keeps open
# standard fd's (0, 1 , 2) so after stop 1 volume fd's should
# be open
nofds=$(ls -lrth /proc/`pgrep glusterfsd`/fd | grep dev/null | wc -l)
TEST [ $((nofds)) -eq 3 ]

cleanup
