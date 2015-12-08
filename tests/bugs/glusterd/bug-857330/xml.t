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
PATTERN="task-id"
TEST check-and-store-task-id-xml

COMMAND="volume status $V0"
PATTERN="id"
EXPECT $TASK_ID get-task-id-xml

COMMAND="volume rebalance $V0 status"
PATTERN="task-id"
EXPECT $TASK_ID get-task-id-xml

## TODO: Add tests for rebalance stop

COMMAND="volume rebalance $V0 status"
PATTERN="completed"
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" get-task-status $PATTERN

###################
## Replace-brick ##
###################
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}2 $H0:$B0/${V0}3 commit force

##################
## Remove-brick ##
##################
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}3

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 start"
PATTERN="task-id"
TEST check-and-store-task-id-xml

COMMAND="volume status $V0"
PATTERN="id"
EXPECT $TASK_ID get-task-id-xml

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 status"
PATTERN="task-id"
EXPECT $TASK_ID get-task-id-xml

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 status"
PATTERN="completed"
EXPECT_WITHIN  $REBALANCE_TIMEOUT "0" get-task-status $PATTERN

## TODO: Add tests for remove-brick stop

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 commit"
PATTERN="task-id"
EXPECT $TASK_ID get-task-id-xml

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
