#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available

TEST mount_nfs $H0:/$V0 $N0 nolock
TEST mkdir -p $N0/foo
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0
TEST mount_nfs $H0:/$V0/foo $N0 nolock
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0
cleanup
