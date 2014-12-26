#!/bin/bash

#Test case: Fail remove-brick 'start' variant when reducing the replica count of a volume.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a 3x3 dist-rep volume
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5,6,7,8};
TEST $CLI volume start $V0

# Mount FUSE and create file/directory
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/zerobytefile.txt
TEST mkdir $M0/test_dir
TEST dd if=/dev/zero of=$M0/file bs=1024 count=1024

function remove_brick_start {
        $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}{1,4,7} start 2>&1|grep -oE 'success|failed'
}

function remove_brick {
        $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}{1,4,7} force 2>&1|grep -oE 'success|failed'
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
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
