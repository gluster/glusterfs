#!/bin/bash

. $(dirname $0)/common.rc
. $(dirname $0)/../../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}1;
TEST $CLI volume info $V0;
TEST $CLI volume start $V0;

TEST glusterfs -s $H0 --volfile-id=$V0 $M0;

TEST $PYTHON $(dirname $0)/../../../utils/create-files.py \
             --multi -b 10 -d 10  -n 10 $M0;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

###############
## Rebalance ##
###############
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}2;

COMMAND="volume rebalance $V0 start"
PATTERN="ID:"
TEST check-and-store-task-id

COMMAND="volume status $V0"
PATTERN="ID"
EXPECT $TASK_ID get-task-id

COMMAND="volume rebalance $V0 status"
PATTERN="completed"
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" get-task-status $PATTERN

###################
## Replace-brick ##
###################
REP_BRICK_PAIR="$H0:$B0/${V0}2 $H0:$B0/${V0}3"

TEST $CLI volume replace-brick $V0 $REP_BRICK_PAIR commit force;

##################
## Remove-brick ##
##################
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}3

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 start"
PATTERN="ID:"
TEST check-and-store-task-id

COMMAND="volume status $V0"
PATTERN="ID"
EXPECT $TASK_ID get-task-id

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 status"
PATTERN="completed"
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" get-task-status $PATTERN

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}3 commit

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
