#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available
TEST mount_nfs $H0:/$V0 $N0

TEST touch $N0/file1
TEST chmod 700 $N0/file1
TEST getfacl $N0/file1

TEST $CLI volume set $V0 root-squash on
TEST getfacl $N0/file1

TEST umount_nfs $H0:/$V0 $N0
TEST mount_nfs $H0:/$V0 $N0
TEST getfacl $N0/file1

## Before killing daemon to avoid deadlocks
umount_nfs $N0

cleanup;

