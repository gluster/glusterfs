#!/bin/bash
#Test that statfs is not served from posix backend FS.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd

#Create brick partitions
TEST truncate -s 100M $B0/brick1
TEST truncate -s 100M $B0/brick2
LO1=`SETUP_LOOP $B0/brick1`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO1
LO2=`SETUP_LOOP $B0/brick2`
TEST [ $? -eq 0 ]
TEST MKFS_LOOP $LO2
TEST mkdir -p $B0/${V0}1 $B0/${V0}2
TEST MOUNT_LOOP $LO1 $B0/${V0}1
TEST MOUNT_LOOP $LO2 $B0/${V0}2

# Create a subdir in mountpoint and use that for volume.
TEST $CLI volume create $V0 $H0:$B0/${V0}1/1 $H0:$B0/${V0}2/1;
TEST $CLI volume start $V0
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0
total_space=$(df -P $M0 | tail -1 | awk '{ print $2}')
# Keeping the size less than 200M mainly because XFS will use
# some storage in brick to keep its own metadata.
TEST [ $total_space -gt 194000 -a $total_space -lt 200000 ]


TEST force_umount $M0
TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status';

# From the same mount point, share another 2 bricks with the volume
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}1/2 $H0:$B0/${V0}2/2 $H0:$B0/${V0}1/3 $H0:$B0/${V0}2/3

TEST $CLI volume start $V0
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0
total_space=$(df -P $M0 | tail -1 | awk '{ print $2}')
TEST [ $total_space -gt 194000 -a $total_space -lt 200000 ]

TEST force_umount $M0
TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;

UMOUNT_LOOP ${B0}/${V0}{1,2}
rm -f ${B0}/brick{1,2}
cleanup;
