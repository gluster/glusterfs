#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

function fop_on_bad_disk {
    local path=$1
    mkdir $path/dir{1..1000} 2>/dev/null
    mv $path/dir1 $path/newdir
    touch $path/foo.txt
    echo $?
}

function ls_fop_on_bad_disk {
    local path=$1
    ls $path
    echo $?
}

TEST init_n_bricks 6;
TEST setup_lvm 6;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 replica 3 $H0:$L1 $H0:$L2 $H0:$L3 $H0:$L4 $H0:$L5 $H0:$L6;
TEST $CLI volume set $V0 health-check-interval 1000;

TEST $CLI volume start $V0;

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;
#corrupt last disk
dd if=/dev/urandom of=/dev/mapper/patchy_snap_vg_6-brick_lvm bs=512K count=200 status=progress && sync


# Test the disk is now returning EIO for touch and ls
EXPECT_WITHIN $DISK_FAIL_TIMEOUT "^1$" fop_on_bad_disk "$L6"
EXPECT_WITHIN $DISK_FAIL_TIMEOUT "^2$" ls_fop_on_bad_disk "$L6"

TEST touch $M0/foo{1..100}
TEST $CLI volume remove-brick $V0 replica 3 $H0:$L4 $H0:$L5 $H0:$L6 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$L4 $H0:$L5 $H0:$L6";

#check that remove-brick status should not have any failed or skipped files
var=`$CLI volume remove-brick $V0 $H0:$L4 $H0:$L5 $H0:$L6 status | grep completed`
TEST [ `echo $var | awk '{print $5}'` = "0"  ]
TEST [ `echo $var | awk '{print $6}'` = "0"  ]

cleanup;
