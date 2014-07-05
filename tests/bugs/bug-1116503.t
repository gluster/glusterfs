#!/bin/bash
#
# Verify that mounting NFS over UDP (MOUNT service only) works.
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc


cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.mount-udp on

TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST mount_nfs $H0:/$V0 $N0 nolock,mountproto=udp,proto=tcp;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;
