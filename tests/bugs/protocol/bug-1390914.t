#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc
cleanup;

#test that fops are not wound on anon-fd when fd is not open on that brick
TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3};
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST $GFS -s $H0 --volfile-id=$V0 --direct-io-mode=enable $M0;

TEST touch $M0/1
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST fd_open 200 'w' "$M0/1"
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

#lk should only happen on 2 bricks, if there is a bug, it will plant a lock
#with anon-fd on first-brick which will never be released because flush won't
#be wound below server xlator for anon-fd
TEST flock -x -n 200
TEST fd_close 200

TEST fd_open 200 'w' "$M0/1"
#this lock will fail if there is a stale lock
TEST flock -x -n 200
TEST fd_close 200
cleanup;
