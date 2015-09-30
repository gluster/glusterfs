#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST mount_nfs $H0:/$V0 $N0 nolock
TEST mkdir -p $N0/a/b/c
TEST umount_nfs $N0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
rmdir $M0/a/b/c
mkdir $M0/a/b/c
TEST mount_nfs $H0:/$V0/a/b/c $N0 nolock
TEST umount_nfs $N0
TEST umount $M0

cleanup
