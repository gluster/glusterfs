#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock
cd $N0

TEST ls

TEST $CLI volume set $V0 nfs.enable-ino32 on
# Main test. This should pass.
TEST ls

cd
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
cleanup

