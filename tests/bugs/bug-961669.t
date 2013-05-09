#!/bin/bash

#Test case: Create a replicate volume; mount and write to it; kill one brick; try to remove the other.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a 1x2 replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1};
TEST $CLI volume start $V0

# Mount FUSE and create file/directory
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/zerobytefile.txt
TEST mkdir $M0/test_dir
TEST dd if=/dev/zero of=$M0/file bs=1024 count=1024

#Kill one of the bricks. This step can be skipped without affecting the outcome of the test.
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-${V0}1.pid`;

function remove_brick_status {
        $CLI volume remove-brick $V0 replica 1 $H0:$B0/${V0}1 start 2>&1
}

#Remove brick
EXPECT "volume remove-brick start: failed: Removing brick from a replicate volume is not allowed" remove_brick_status;

#Check the volume type
EXPECT "Replicate" echo `$CLI volume info |grep Type |awk '{print $2}'`

TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
