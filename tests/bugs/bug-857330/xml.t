#!/bin/bash

. $(dirname $0)/common.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}1;
TEST $CLI volume info $V0;
TEST $CLI volume start $V0;

TEST glusterfs -s $H0 --volfile-id=$V0 $M0;

TEST python2 $(dirname $0)/../../utils/create-files.py --multi -b 10 -d 10  -n 10 $M0;

TEST umount $M0;


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
EXPECT_WITHIN 300 $PATTERN get-task-status

###################
## Replace-brick ##
###################
REP_BRICK_PAIR="$H0:$B0/${V0}2 $H0:$B0/${V0}3"

COMMAND="volume replace-brick $V0 $REP_BRICK_PAIR start"
PATTERN="task-id"
TEST check-and-store-task-id-xml

COMMAND="volume status $V0"
PATTERN="id"
EXPECT $TASK_ID get-task-id-xml

COMMAND="volume replace-brick $V0 $REP_BRICK_PAIR status"
PATTERN="task-id"
EXPECT $TASK_ID get-task-id-xml

## TODO: Add more tests for replace-brick pause|abort

COMMAND="volume replace-brick $V0 $REP_BRICK_PAIR status"
PATTERN="complete"
EXPECT_WITHIN 300 $PATTERN get-task-status

COMMAND="volume replace-brick $V0 $REP_BRICK_PAIR commit"
PATTERN="task-id"
EXPECT $TASK_ID get-task-id-xml

##################
## Remove-brick ##
##################
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
EXPECT_WITHIN 300 $PATTERN get-task-status

## TODO: Add tests for remove-brick stop

COMMAND="volume remove-brick $V0 $H0:$B0/${V0}3 commit"
PATTERN="task-id"
EXPECT $TASK_ID get-task-id-xml

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
