#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

function create_bricks {
    TEST truncate -s 100M $B0/brick0
    TEST truncate -s 100M $B0/brick1
    TEST truncate -s 20M $B0/brick2
    LO1=`SETUP_LOOP $B0/brick0`
    TEST [ $? -eq 0 ]
    TEST MKFS_LOOP $LO1
    LO2=`SETUP_LOOP $B0/brick1`
    TEST [ $? -eq 0 ]
    TEST MKFS_LOOP $LO2
    LO3=`SETUP_LOOP $B0/brick2`
    TEST [ $? -eq 0 ]
    TEST MKFS_LOOP $LO3
    TEST mkdir -p $B0/${V0}0 $B0/${V0}1 $B0/${V0}2
    TEST MOUNT_LOOP $LO1 $B0/${V0}0
    TEST MOUNT_LOOP $LO2 $B0/${V0}1
    TEST MOUNT_LOOP $LO3 $B0/${V0}2
}

function create_files {
        local i=1
        while (true)
        do
                touch $M0/file$i
                if [ -e $B0/${V0}2/file$i ];
                then
                        ((i++))
                else
                        break
                fi
        done
}

TESTS_EXPECTED_IN_LOOP=13

#Arbiter volume: Check for ENOSPC when arbiter brick becomes full#
TEST glusterd
create_bricks
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 performance.write-behind off
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0

create_files
TEST kill_brick $V0 $H0 $B0/${V0}1
error1=$(touch $M0/file-1 2>&1)
EXPECT "No space left on device" echo $error1
error2=$(mkdir $M0/dir-1 2>&1)
EXPECT "No space left on device" echo $error2
error3=$((echo "Test" > $M0/file-3) 2>&1)
EXPECT "No space left on device" echo $error3

cleanup

#Replica-3 volume: Check for ENOSPC when one of the brick becomes full#
#Keeping the third brick of lower size to simulate disk full scenario#
TEST glusterd
create_bricks
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 performance.write-behind off
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0

create_files
TEST kill_brick $V0 $H0 $B0/${V0}1
error1=$(touch $M0/file-1 2>&1)
EXPECT "No space left on device" echo $error1
error2=$(mkdir $M0/dir-1 2>&1)
EXPECT "No space left on device" echo $error2
error3=$((cat /dev/zero > $M0/file1) 2>&1)
EXPECT "No space left on device" echo $error3

cleanup
