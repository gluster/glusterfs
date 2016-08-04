#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

cleanup

#This script checks command "gluster volume rebalance <volname> status will not
#show any output when user have done only remove-brick start and command
#'gluster volume remove-brick <volname> <brick_name> status' will not show
#any output when user have triggered only rebalance start.

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}
TEST $CLI volume start $V0

TEST $CLI volume rebalance $V0 start
TEST ! $CLI volume remove-brick $V0 $H0:$B0/${V0}1 status
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start
TEST ! $CLI volume rebalance $V0 status

cleanup
