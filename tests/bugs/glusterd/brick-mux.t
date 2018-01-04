#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

cleanup

#bug-1444596 - validating brick mux

TEST glusterd -LDEBUG
TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume create $V1 $H0:$B0/brick{2,3}

TEST $CLI volume set all cluster.brick-multiplex on

TEST $CLI volume start $V0
TEST $CLI volume start $V1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 online_brick_count
EXPECT 1 count_brick_processes

#bug-1499509 - stop all the bricks when a brick process is killed
kill -9 $(pgrep glusterfsd)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 online_brick_count

TEST $CLI volume start $V0 force
TEST $CLI volume start $V1 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 online_brick_count


pkill glusterd
TEST glusterd

#Check brick status after restart glusterd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 online_brick_count
EXPECT 1 count_brick_processes

TEST $CLI volume set $V1 performance.cache-size 32MB
TEST $CLI volume stop $V1
TEST $CLI volume start $V1

#Check No. of brick processes after change option
EXPECT 2 count_brick_processes

pkill glusterd
TEST glusterd

#Check brick status after restart glusterd should not be NA
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 online_brick_count
EXPECT 2 count_brick_processes

pkill glusterd
TEST glusterd

#Check brick status after restart glusterd should not be NA
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 online_brick_count
EXPECT 2 count_brick_processes

#bug-1444596_brick_mux_posix_hlth_chk_status

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST rm -rf $H0:$B0/brick{0,1}

#Check No. of brick processes after remove brick from back-end
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 online_brick_count

TEST glusterfs -s $H0 --volfile-id $V1 $M0
TEST touch $M0/file{1..10}

pkill glusterd
TEST glusterd -LDEBUG
sleep 5
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 online_brick_count

cleanup

