#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{1..4}
TEST $CLI volume start $V0
TEST $CLI volume remove-brick $V0 $H0:$B0/brick{3..4} start
EXPECT_WITHIN 10 "completed" remove_brick_status_completed_field  "$V0 $H0:$B0/brick3"
EXPECT_WITHIN 10 "completed" remove_brick_status_completed_field  "$V0 $H0:$B0/brick4"
TEST $CLI volume remove-brick $V0 $H0:$B0/brick{3..4} commit
TEST $CLI volume remove-brick $V0 replica 1 $H0:$B0/brick2 force

cleanup;
