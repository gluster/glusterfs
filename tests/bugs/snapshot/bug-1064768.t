#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick0 $H0:$B0/brick1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume profile $V0 start
TEST $CLI volume profile $V0 info
TEST $CLI volume profile $V0 stop

TEST $CLI volume status
TEST $CLI volume stop $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Stopped' volinfo_field $V0 'Status';
cleanup;
