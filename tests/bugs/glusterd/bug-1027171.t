#!/bin/bash

#Test case: Do not allow commit if the bricks are not decommissioned

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a Distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

#Remove bricks and commit without starting
function remove_brick_commit_status {
        $CLI volume remove-brick $V0 \
        $H0:$B0/${V0}2 commit 2>&1 |grep -oE "success|decommissioned"
}
EXPECT "decommissioned"  remove_brick_commit_status;

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST ! $CLI volume info $V0

#Create a Distributed-Replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..4};
TEST $CLI volume start $V0

#Try to reduce replica count with start option
function remove_brick_start_status {
        $CLI volume remove-brick $V0 replica 1 \
        $H0:$B0/${V0}1  $H0:$B0/${V0}3 start 2>&1 |grep -oE "success|failed"
}
EXPECT "failed"  remove_brick_start_status;

#Remove bricks with commit option
function remove_brick_commit_status2 {
        $CLI volume remove-brick $V0 replica 1 \
        $H0:$B0/${V0}1  $H0:$B0/${V0}3 commit 2>&1  |
        grep -oE "success|decommissioned"
}
EXPECT "decommissioned"  remove_brick_commit_status2;

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST ! $CLI volume info $V0

cleanup;
