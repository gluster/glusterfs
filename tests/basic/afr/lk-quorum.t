#!/bin/bash

SCRIPT_TIMEOUT=300

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc
cleanup;

TEST glusterd;
TEST pidof glusterd

#Tests for quorum-type option for replica 2
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1};
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0 --direct-io-mode=enable $M0;

TEST touch $M0/a

#When all bricks are up, lock and unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST fd_close $fd1

#When all bricks are down, lock/unlock should fail
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST $CLI volume stop $V0
TEST ! flock -x $fd1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#Check locking behavior with quorum 'fixed' and quorum-count 2
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^fixed$" mount_get_option_value $M0 $V0-replicate-0 quorum-type
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^2$" mount_get_option_value $M0 $V0-replicate-0 quorum-count

#When all bricks are up, lock and unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST fd_close $fd1

#When all bricks are down, lock/unlock should fail
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST $CLI volume stop $V0
TEST ! flock -x $fd1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#When any of the bricks is down lock/unlock should fail
#kill first brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST ! flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST fd_close $fd1

#kill 2nd brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#Check locking behavior with quorum 'fixed' and quorum-count 1
TEST $CLI volume set $V0 cluster.quorum-count 1
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^1$" mount_get_option_value $M0 $V0-replicate-0 quorum-count

#When all bricks are up, lock and unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST fd_close $fd1

#When all bricks are down, lock/unlock should fail
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST $CLI volume stop $V0
TEST ! flock -x $fd1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#When any of the bricks is down lock/unlock should succeed
#kill first brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST fd_close $fd1

#kill 2nd brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#Check locking behavior with quorum 'auto'
TEST $CLI volume set $V0 cluster.quorum-type auto
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^auto$" mount_get_option_value $M0 $V0-replicate-0 quorum-type

#When all bricks are up, lock and unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST fd_close $fd1

#When all bricks are down, lock/unlock should fail
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST $CLI volume stop $V0
TEST ! flock -x $fd1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#When first brick is down lock/unlock should fail
#kill first brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST ! flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST fd_close $fd1

#When second brick is down lock/unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

cleanup;
TEST glusterd;
TEST pidof glusterd

#Tests for replica 3
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0 --direct-io-mode=enable $M0;

TEST touch $M0/a

#When all bricks are up, lock and unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST fd_close $fd1

#When all bricks are down, lock/unlock should fail
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST $CLI volume stop $V0
TEST ! flock -x $fd1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST fd_close $fd1

#When any of the bricks is down lock/unlock should succeed
#kill first brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST fd_close $fd1

#kill 2nd brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#kill 3rd brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST fd_close $fd1

#When any two of the bricks are down lock/unlock should fail
#kill first,second bricks
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST fd_close $fd1

#kill 2nd,3rd bricks
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST ! flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST fd_close $fd1

#kill 1st,3rd brick
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST ! flock -x $fd1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST fd_close $fd1

cleanup
