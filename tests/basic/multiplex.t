#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../traps.rc
. $(dirname $0)/../volume.rc

function count_up_bricks {
        $CLI --xml volume status $V0 | grep '<status>1' | wc -l
}

function count_brick_processes {
	pgrep glusterfsd | wc -l
}

function count_brick_pids {
        $CLI --xml volume status $V0 | sed -n '/.*<pid>\([^<]*\).*/s//\1/p' \
                                     | grep -v "N/A" | sort | uniq | wc -l
}

TEST glusterd
TEST $CLI volume set all cluster.brick-multiplex on
push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 features.trash enable

TEST $CLI volume start $V0
# Without multiplexing, there would be two.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks
EXPECT 1 count_brick_processes

TEST $CLI volume stop $V0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 0 count_brick_processes
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks
EXPECT 1 count_brick_processes

TEST kill_brick $V0 $H0 $B0/brick1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 1 count_up_bricks
# Make sure the whole process didn't go away.
EXPECT 1 count_brick_processes

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks
EXPECT 1 count_brick_processes

# Killing the first brick is a bit more of a challenge due to socket-path
# issues.
TEST kill_brick $V0 $H0 $B0/brick0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 1 count_up_bricks
EXPECT 1 count_brick_processes
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks
EXPECT 1 count_brick_processes

# Make sure that the two bricks show the same PID.
EXPECT 1 count_brick_pids

# Do a quick test to make sure that the bricks are acting as separate bricks
# even though they're in the same process.
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
for i in $(seq 10 99); do
        echo hello > $M0/file$i
done
nbrick0=$(ls $B0/brick0/file?? | wc -l)
nbrick1=$(ls $B0/brick1/file?? | wc -l)
TEST [ $((nbrick0 + nbrick1)) -eq 90 ]
TEST [ $((nbrick0 * nbrick1)) -ne 0 ]
