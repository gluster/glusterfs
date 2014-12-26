#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function rebalance_start {
         $CLI volume rebalance $1 start | head -1;
}

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1

TEST $CLI volume start $V0

EXPECT "volume rebalance: $V0: success: Rebalance on $V0 has \
been started successfully. Use rebalance status command to \
check status of the rebalance process." rebalance_start $V0

cleanup;
