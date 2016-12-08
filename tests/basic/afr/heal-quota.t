#!/bin/bash

#This file tests that heal succeeds even when quota is exceeded

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 10MB
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

TEST touch $M0/a $M0/b
dd if=/dev/zero of=$M0/b bs=1M count=7
TEST kill_brick $V0 $H0 $B0/${V0}0
dd if=/dev/zero of=$M0/a bs=1M count=12 #This shall fail
TEST $CLI volume start $V0 force
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

cleanup
