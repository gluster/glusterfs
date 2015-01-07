#!/bin/bash

#Test case: Create a distributed replicate volume, and remove multiple
#replica pairs in a single remove-brick command.

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
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
