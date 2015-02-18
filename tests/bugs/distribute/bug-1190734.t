#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

BRICK_COUNT=3
FILE_COUNT=100

function create_files {
    rm -rf $2
    mkdir $2
    for i in `seq 1 $1`; do
        touch $2/file_$i
    done
}

function check_file_count {
    ORIG_FILE_COUNT=`find $2 | tail -n +2 |wc -l`
    [ $ORIG_FILE_COUNT -eq $1 ]
}

function reset {
    $CLI volume stop $V0
    umount $1
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

function start_removing_bricks {
    check_file_count $FILE_COUNT $1
    [ $? -ne 0 ] && return 1
    $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}2  $H0:$B0/${V0}3 start
    [ $? -ne 0 ] && return 1

    return 0
}

function finish_removing_bricks {

    $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}2  $H0:$B0/${V0}3 commit
    [ $? -ne 0 ] && return 1

    check_file_count $FILE_COUNT $1
    return $?
}

cleanup

TEST glusterd
TEST pidof glusterd

# Test 1-2 Create repliacted volume

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
    $H0:$B0/${V0}2 $H0:$B0/${V0}3 $H0:$B0/${V0}4 $H0:$B0/${V0}5

# ------- test 1: AFR, fuse + remove bricks

TEST start_mount_fuse test1
TEST start_removing_bricks $M0/test1
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/${V0}2  $H0:$B0/${V0}3"
$CLI  volume remove-brick $V0 replica 2  $H0:$B0/${V0}2  $H0:$B0/${V0}3 status > /tmp/out
TEST finish_removing_bricks $M0/test1
reset $M0

# ------- test 2: AFR, nfs + remove bricks

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
    $H0:$B0/${V0}2 $H0:$B0/${V0}3 $H0:$B0/${V0}4 $H0:$B0/${V0}5

TEST start_mount_nfs test2
TEST start_removing_bricks $N0/test2
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/${V0}2  $H0:$B0/${V0}3"
TEST finish_removing_bricks $N0/test2
reset $N0

cleanup
