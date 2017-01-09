#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}1

#add-brick should fail
TEST ! $CLI_NO_FORCE volume add-brick $V0 replica 3 $H0:$B0/${V0}3

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}3

TEST $CLI volume create $V1 $H0:$B0/${V1}{1,2};
TEST $CLI volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}2
TEST kill_brick $V1 $H0 $B0/${V1}1

#add-brick should fail
TEST ! $CLI_NO_FORCE volume add-brick $V1 replica 2 $H0:$B0/${V1}{3,4}

TEST $CLI volume start $V1 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V1 $H0 $B0/${V1}2
TEST $CLI volume add-brick $V1 replica 2 $H0:$B0/${V1}{3,4}
cleanup;
