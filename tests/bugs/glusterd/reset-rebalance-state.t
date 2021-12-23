#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc


get_rebalance_status() {
    $CLI volume status $V0 | egrep ^"Status   " | awk '{print $3}'
}

run_rebal_check_status() {
    TEST $CLI volume rebalance $V0 start
    EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
    REBAL_STATE=$(get_rebalance_status)
    TEST [ $REBAL_STATE == "completed" ]
}

replace_brick_check_status() {
    TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1_replace commit force
    REBAL_STATE=$(get_rebalance_status)
    TEST [ $REBAL_STATE == "reset" ]
}

reset_brick_check_status() {
    TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}2 start
    TEST $CLI volume reset-brick $V0 $H0:$B0/${V0}2 $H0:$B0/${V0}2 commit force
    REBAL_STATE=$(get_rebalance_status)
    TEST [ $REBAL_STATE == "reset" ]
}

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume info;
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..6} force;
TEST $CLI volume start $V0;

run_rebal_check_status;
replace_brick_check_status;
reset_brick_check_status;

cleanup;

