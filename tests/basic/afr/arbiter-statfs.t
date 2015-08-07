#!/bin/bash
#Test that statfs is not served from the arbiter brick.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd

#Create brick partitions
TEST truncate -s 1G $B0/brick1
TEST truncate -s 1G $B0/brick2
#Arbiter brick is of a lesser size.
TEST truncate -s 90M $B0/brick3
TEST LO1=`SETUP_LOOP $B0/brick1`
TEST MKFS_LOOP $LO1
TEST LO2=`SETUP_LOOP $B0/brick2`
TEST MKFS_LOOP $LO2
TEST LO3=`SETUP_LOOP $B0/brick3`
TEST MKFS_LOOP $LO3
TEST mkdir -p $B0/${V0}1 $B0/${V0}2 $B0/${V0}3
TEST MOUNT_LOOP $LO1 $B0/${V0}1
TEST MOUNT_LOOP $LO2 $B0/${V0}2
TEST MOUNT_LOOP $LO3 $B0/${V0}3

TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{1,2,3};
TEST $CLI volume start $V0
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
free_space=$(df /home | tail -1 | awk '{ print $4}')
TEST [ $free_space -gt 100000 ]
TEST force_umount $M0
TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume delete $V0;
UMOUNT_LOOP ${B0}/${V0}{1,2,3}
rm -f ${B0}/brick{1,2,3}
cleanup;
