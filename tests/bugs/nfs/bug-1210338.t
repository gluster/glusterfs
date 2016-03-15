#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

NFS_SOURCE=$(dirname $0)/bug-1210338.c
NFS_EXEC=$(dirname $0)/excl_create

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock

build_tester $NFS_SOURCE -o $NFS_EXEC
TEST [ -e $NFS_EXEC ]

TEST $NFS_EXEC $N0/my_file

rm -f $NFS_EXEC;

cleanup
