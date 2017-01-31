#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
. $(dirname $0)/../../nfs.rc
cleanup;

#Check that mounting fails when only arbiter brick is up.

TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1

# Doing `mount -t glusterfs $H0:$V0 $M0` fails right away but doesn't work on NetBSD
# So check that stat <mount> fails instead.
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST ! stat $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

mount_nfs $H0:/$V0 $N0
TEST [ $? -ne 0 ]

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST  stat $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

mount_nfs $H0:/$V0 $N0
TEST [ $? -eq 0 ]
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup
