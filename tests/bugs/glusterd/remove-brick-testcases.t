#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

function brick_count()
{
    local vol=$1;

    $CLI volume info $vol | egrep "^Brick[0-9]+: " | wc -l;
}

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..5}
TEST $CLI volume start $V0

#bug-1225716 - remove-brick on a brick which is down should fail
#kill a brick process
kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" brick_up_status $V0 $H0 $B0/${V0}1

#remove-brick start should fail as the brick is down
TEST ! $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

#remove-brick start should succeed as the brick is up
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0 $H0:$B0/${V0}1"

#kill a brick process
kill_brick $V0 $H0 $B0/${V0}1

#remove-brick commit should pass even if the brick is down
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 commit

#bug-1121584 - brick-existing-validation-for-remove-brick-status-stop
## Start remove-brick operation on the volume
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 start

## By giving non existing brick for remove-brick status/stop command should
## give error.
TEST ! $CLI volume remove-brick $V0 $H0:$B0/ABCD status
TEST ! $CLI volume remove-brick $V0 $H0:$B0/ABCD stop

## By giving brick which is part of volume for remove-brick status/stop command
## should print statistics of remove-brick operation or stop remove-brick
## operation.
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 status
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 stop

#bug-878004 - validate remove brick force
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 force;
EXPECT '3' brick_count $V0

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}3 force;
EXPECT '2' brick_count $V0

#bug-1027171 - Do not allow commit if the bricks are not decommissioned
#Remove bricks and commit without starting
function remove_brick_commit_status {
        $CLI volume remove-brick $V0 \
        $H0:$B0/${V0}4 commit 2>&1 |grep -oE "success|decommissioned"
}
EXPECT "decommissioned"  remove_brick_commit_status;

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;

#Create a 2X3 distributed-replicate volume
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..6};
TEST $CLI volume start $V0

#Try to reduce replica count with start option
function remove_brick_start_status {
        $CLI volume remove-brick $V0 replica 2 \
        $H0:$B0/${V0}3  $H0:$B0/${V0}6 start 2>&1 |grep -oE "success|failed"
}
EXPECT "failed"  remove_brick_start_status;

#Remove bricks with commit option
function remove_brick_commit_status2 {
        $CLI volume remove-brick $V0 replica 2 \
        $H0:$B0/${V0}3  $H0:$B0/${V0}6 commit 2>&1  |
        grep -oE "success|decommissioned"
}
EXPECT "decommissioned"  remove_brick_commit_status2;
TEST $CLI volume info $V0

#bug-1040408 - reduce replica count of distributed replicate volume

# Reduce to 2x2 volume by specifying bricks in reverse order
function remove_brick_status {
        $CLI volume remove-brick $V0 replica 2 \
        $H0:$B0/${V0}6  $H0:$B0/${V0}3 force 2>&1 |grep -oE "success|failed"
}
EXPECT "success"  remove_brick_status;
TEST $CLI volume info $V0

#bug-1120647 - remove brick validation

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}{4..5} start
EXPECT_WITHIN 10 "completed" remove_brick_status_completed_field  "$V0 $H0:$B0/${V0}5"
EXPECT_WITHIN 10 "completed" remove_brick_status_completed_field  "$V0 $H0:$B0/${V0}4"
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}{4..5} commit
TEST $CLI volume remove-brick $V0 replica 1 $H0:$B0/${V0}2 force

cleanup
