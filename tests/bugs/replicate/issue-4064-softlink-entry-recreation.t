#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
echo "Data">$M0/FILE
ret=$?
TEST [ $ret -eq 0 ]

TEST kill_brick $V0 $H0 $B0/${V0}2

TEST ln -s $M0/FILE $M0/SOFT
TEST ln  $M0/FILE $M0/HARD
#hardlink to a softlink
TEST ln $M0/SOFT $M0/SOFTHARD

#Set a metadata heal on the softlink
TEST chown -h root:root $M0/SOFT

#start the brick
TEST $CLI volume start $V0 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2

TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup;
