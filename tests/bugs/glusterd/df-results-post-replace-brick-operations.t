#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup
TEST glusterd

#Create brick partitions
TEST truncate -s 100M $B0/brick1
TEST truncate -s 100M $B0/brick2
TEST truncate -s 100M $B0/brick3
TEST truncate -s 100M $B0/brick4
TEST truncate -s 100M $B0/brick5

LO1=`SETUP_LOOP $B0/brick1`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO1

LO2=`SETUP_LOOP $B0/brick2`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO2

LO3=`SETUP_LOOP $B0/brick3`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO3

LO4=`SETUP_LOOP $B0/brick4`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO4

LO5=`SETUP_LOOP $B0/brick5`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO5

TEST mkdir -p $B0/${V0}1 $B0/${V0}2 $B0/${V0}3 $B0/${V0}4 $B0/${V0}5
TEST MOUNT_LOOP $LO1 $B0/${V0}1
TEST MOUNT_LOOP $LO2 $B0/${V0}2
TEST MOUNT_LOOP $LO3 $B0/${V0}3
TEST MOUNT_LOOP $LO4 $B0/${V0}4
TEST MOUNT_LOOP $LO5 $B0/${V0}5

# create a subdirectory in mount point and use it for volume creation
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}1/brick1 $H0:$B0/${V0}2/brick1 $H0:$B0/${V0}3/brick1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" online_brick_count

# mount the volume and check the size at mount point
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0
total_space=$(df -P $M0 | tail -1 | awk '{ print $2}')

# perform replace brick operations
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1/brick1 $H0:$B0/${V0}4/brick1 commit force
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}2/brick1 $H0:$B0/${V0}5/brick1 commit force

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

# check for the size at mount point, it should be same as previous
total_space_new=$(df -P $M0 | tail -1 | awk '{ print $2}')
TEST [ $total_space -eq $total_space_new ]
