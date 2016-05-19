#!/bin/bash
#
# Verify that mounting a subdir over NFS works, even with a trailing /
#
# For example:
#    mount -t nfs server.example.com:/volume/subdir/
#

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc


cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

TEST mount_nfs $H0:/$V0 $N0 nolock
TEST mkdir -p $N0/subdir
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

TEST mount_nfs $H0:/$V0/subdir/ $N0 nolock
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup
