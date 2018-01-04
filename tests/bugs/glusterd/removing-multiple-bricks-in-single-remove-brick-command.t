#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a 3X2 distributed-replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..6};
TEST $CLI volume start $V0

#bug-974007 - remove multiple replica pairs in a single brick command
# Mount FUSE and create files
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/file{1..10}

# Remove bricks from two sub-volumes to make it a 1x2 vol.
# Bricks in question are given in a random order but from the same subvols.
function remove_brick_start_status {
        $CLI volume remove-brick $V0 \
        $H0:$B0/${V0}6  $H0:$B0/${V0}1 \
        $H0:$B0/${V0}2  $H0:$B0/${V0}5 start 2>&1 |grep -oE "success|failed"
}
EXPECT "success"  remove_brick_start_status;

# Wait for rebalance to complete
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/${V0}6 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}5"

# Check commit status
function remove_brick_commit_status {
        $CLI volume remove-brick $V0 \
         $H0:$B0/${V0}6  $H0:$B0/${V0}1 \
         $H0:$B0/${V0}2  $H0:$B0/${V0}5 commit 2>&1 |grep -oE "success|failed"
}
EXPECT "success" remove_brick_commit_status;


# Check the volume type
EXPECT "Replicate" echo `$CLI volume info |grep Type |awk '{print $2}'`
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#bug-961669 - remove brick start should fail when reducing the replica count

#Create a 3x3 dist-rep volume
TEST $CLI volume create $V1 replica 3 $H0:$B0/${V1}{0,1,2,3,4,5,6,7,8};
TEST $CLI volume start $V1

# Mount FUSE and create file/directory
TEST glusterfs -s $H0 --volfile-id $V1 $M0
TEST touch $M0/zerobytefile.txt
TEST mkdir $M0/test_dir
TEST dd if=/dev/zero of=$M0/file bs=1024 count=1024

function remove_brick_start {
        $CLI volume remove-brick $V1 replica 2 $H0:$B0/${V1}{1,4,7} start 2>&1|grep -oE 'success|failed'
}

function remove_brick {
        $CLI volume remove-brick $V1 replica 2 $H0:$B0/${V1}{1,4,7} force 2>&1|grep -oE 'success|failed'
}

#remove-brick start variant
#Actual message displayed at cli is:
#"volume remove-brick start: failed: Rebalancing not needed when reducing replica count. Try without the 'start' option"
EXPECT "failed" remove_brick_start;

#remove-brick commit-force
#Actual message displayed at cli is:
#"volume remove-brick commit force: success"
EXPECT "success" remove_brick

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup;
