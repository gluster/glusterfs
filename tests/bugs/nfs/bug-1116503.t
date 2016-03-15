#!/bin/bash
#
# Verify that mounting NFS over UDP (MOUNT service only) works.
#

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc


cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume set $V0 nfs.mount-udp on

TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST mount_nfs $H0:/$V0 $N0 nolock,mountproto=udp,proto=tcp;
TEST mkdir -p $N0/foo/bar
TEST ls $N0/foo
TEST ls $N0/foo/bar
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0/foo $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0/foo/bar $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-allow $H0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0/foo/bar $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

TEST $CLI volume set $V0 nfs.rpc-auth-reject $H0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST ! mount_nfs $H0:/$V0/foo/bar $N0 nolock,mountproto=udp,proto=tcp;

cleanup;
