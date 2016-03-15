#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

BRICK_COUNT=3
FILE_COUNT=100
FILE_COUNT_TIME=5

function create_files {
    rm -rf $2
    mkdir $2
    for i in `seq 1 $1`; do
        touch $2/file_$i
    done
}

function get_file_count {
    ls $1/file_[0-9]* | wc -l
}

function reset {
    $CLI volume stop $V0
    ${UMOUNT_F} $1
    $CLI volume delete $V0
}

function start_mount_fuse {
    $CLI volume start $V0
    [ $? -ne 0 ] && return 1

    $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
    [ $? -ne 0 ] && return 1

    create_files $FILE_COUNT $M0/$1
    [ $? -ne 0 ] && return 1

    return 0
}

function start_mount_nfs {
    $CLI volume start $V0
    [ $? -ne 0 ] && return 1

    sleep 3
    mount_nfs $H0:/$V0 $N0
    [ $? -ne 0 ] && return 1

    create_files $FILE_COUNT $N0/$1
    [ $? -ne 0 ] && return 1

    return 0
}

cleanup

TEST glusterd
TEST pidof glusterd

# Test 1-2 Create repliacted volume

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
    $H0:$B0/${V0}2 $H0:$B0/${V0}3 $H0:$B0/${V0}4 $H0:$B0/${V0}5
TEST $CLI volume set $V0 nfs.disable false

# ------- test 1: AFR, fuse + remove bricks

TEST start_mount_fuse test1
EXPECT_WITHIN $FILE_COUNT_TIME $FILE_COUNT get_file_count $M0/test1
TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}{2,3} start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/${V0}2  $H0:$B0/${V0}3"
TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}{2,3} commit
EXPECT_WITHIN $FILE_COUNT_TIME $FILE_COUNT get_file_count $M0/test1
reset $M0

# ------- test 2: AFR, nfs + remove bricks

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
    $H0:$B0/${V0}2 $H0:$B0/${V0}3 $H0:$B0/${V0}4 $H0:$B0/${V0}5
TEST $CLI volume set $V0 nfs.disable false

TEST start_mount_nfs test2
EXPECT_WITHIN $FILE_COUNT_TIME $FILE_COUNT get_file_count $N0/test2
TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}2  $H0:$B0/${V0}3 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/${V0}2  $H0:$B0/${V0}3"
TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}{2,3} commit
EXPECT_WITHIN $FILE_COUNT_TIME $FILE_COUNT get_file_count $N0/test2
reset $N0

cleanup
