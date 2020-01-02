#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup

GET_MDATA_PATH=$(dirname $0)/../../utils
build_tester $GET_MDATA_PATH/get-mdata-xattr.c

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0..2}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST touch $M0/a
sleep 1
TEST kill_brick $V0 $H0 $B0/brick0
TEST touch $M0/a

EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

mtime0=$(get_mtime $B0/brick0/a)
mtime1=$(get_mtime $B0/brick1/a)
TEST [ $mtime0 -eq $mtime1 ]

ctime0=$(get_ctime $B0/brick0/a)
ctime1=$(get_ctime $B0/brick1/a)
TEST [ $ctime0 -eq $ctime1 ]

###############################################################################
# Repeat the test with ctime feature disabled.
TEST $CLI volume set $V0 features.ctime off

TEST touch $M0/b
sleep 1
TEST kill_brick $V0 $H0 $B0/brick0
TEST touch $M0/b

EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

mtime2=$(get_mtime $B0/brick0/b)
mtime3=$(get_mtime $B0/brick1/b)
TEST [ $mtime2 -eq $mtime3 ]

TEST rm $GET_MDATA_PATH/get-mdata-xattr

TEST force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
