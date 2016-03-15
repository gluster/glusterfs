#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume set $V0 uss on
TEST $CLI volume start $V0

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

## Mount NFS
TEST mount_nfs $H0:/$V0 $N0 nolock;

TEST df -h $N0

cleanup;
