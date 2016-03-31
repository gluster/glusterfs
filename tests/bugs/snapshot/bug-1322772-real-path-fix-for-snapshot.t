#!/bin/bash

. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../include.rc
cleanup;

TEST verify_lvm_version
TEST init_n_bricks 1
TEST setup_lvm 1

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$L1
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $CLI snapshot create ${V0}_snap $V0

# Simulate a node reboot by unmounting the brick, snap_brick and followed by
# deleting the brick. Now once glusterd restarts, it should be able to construct
# and remount the snap brick
snap_brick=`gluster snap status | grep "Brick Path" | awk -F ":"  '{print $3}'`

pkill gluster
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $L1
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $snap_brick
rm -rf $snap_brick

TEST glusterd
TEST pidof glusterd
