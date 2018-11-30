#!/bin/bash

#Test the split-brain resolution CLI commands.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

#Create replica 2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/file

############ Healing using favorite-child-policy = size and size of bricks is same #################
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

#file still in split-brain
EXPECT_WITHIN $HEAL_TIMEOUT "2" get_pending_heal_count $V0
cat $M0/file > /dev/null
EXPECT_NOT "^0$" echo $?

#We know that both bricks have same size file
TEST $CLI volume set $V0 cluster.favorite-child-policy size
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "2" get_pending_heal_count $V0
cat $M0/file > /dev/null
EXPECT_NOT "^0$" echo $?

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup

