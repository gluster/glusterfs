#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3};
TEST $CLI volume start $V0;

# Start a remove-brick and try to start a rebalance/remove-brick without committing
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start

TEST ! $CLI volume rebalance $V0 start
TEST ! $CLI volume remove-brick $V0 $H0:$B0/${V0}2 start

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field \
"$V0" "$H0:$B0/${V0}1"
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 commit

gluster volume status

TEST $CLI volume rebalance $V0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
TEST $CLI volume rebalance $V0 stop

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 start
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 stop

TEST $CLI volume stop $V0

cleanup;

