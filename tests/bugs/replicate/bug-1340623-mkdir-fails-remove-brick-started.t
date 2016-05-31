#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc
cleanup;

TEST glusterd
TEST pidof glusterd

## Create a 2x2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/r11 $H0:$B0/r12 $H0:$B0/r21 $H0:$B0/r22;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## for release > 3.7 , gluster nfs is off by default
TEST $CLI vol set $V0 nfs.disable off;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST mount_nfs $H0:/$V0 $N0;

## create some directories and files inside mount
mkdir $N0/io;
for j in {1..10}; do mkdir $N0/io/b$j; for k in {1..10}; do touch $N0/io/b$j/c$k; done done

TEST $CLI volume remove-brick $V0 $H0:$B0/r11 $H0:$B0/r12 start;

TEST mkdir $N0/dir1;

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/r11 $H0:$B0/r12"

TEST $CLI volume remove-brick $V0 $H0:$B0/r11 $H0:$B0/r12 commit;

TEST mkdir $N0/dir2;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;
