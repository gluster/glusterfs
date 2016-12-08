#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd

#Create replica 2 volume and create file/dir.
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume set $V0 self-heal-daemon off
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST mkdir  $M0/dir1
TEST dd if=/dev/urandom of=$M0/file1 bs=1024 count=1

#Kill second brick and perform I/O to have pending heals.
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST mkdir $M0/dir2
TEST dd if=/dev/urandom of=$M0/file1 bs=1024 count=1024


#convert replica 2 to arbiter volume
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

#syntax check for add-brick.
TEST ! $CLI volume add-brick $V0 replica 2 arbiter 1 $H0:$B0/${V0}2
TEST ! $CLI volume add-brick $V0 replica 3 arbiter 2 $H0:$B0/${V0}2

TEST $CLI volume add-brick $V0 replica 3 arbiter 1 $H0:$B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2

#Heal files
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#Perform I/O after add-brick
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST mkdir $M0/dir3
TEST dd if=/dev/urandom of=$M0/file2 bs=1024 count=1024

# File hierarchy must be same in all 3 bricks.
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}2 | sort)
TEST diff <(ls $B0/${V0}1 | sort) <(ls $B0/${V0}2 | sort)

#Mount serves the correct file size
EXPECT "1048576" stat -c %s $M0/file1
EXPECT "1048576" stat -c %s $M0/file2

#Check file size in arbiter brick
EXPECT "0" stat -c %s $B0/${V0}2/file1
EXPECT "0" stat -c %s $B0/${V0}2/file2

TEST force_umount $M0
cleanup;
