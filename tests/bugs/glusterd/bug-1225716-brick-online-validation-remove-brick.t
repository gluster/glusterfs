#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

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
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" brick_up_status $V0 $H0 $B0/${V0}1

#remove-brick commit should pass even if the brick is down
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 commit

cleanup;
