#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc
cleanup

#1
TEST glusterd
TEST pidof glusterd
#3
TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 nfs.drc on
TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock
cd $N0
#7
TEST dbench -t 10 10
TEST rm -rf $N0/*
cd
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
#10
TEST $CLI volume set $V0 nfs.drc-size 10000
cleanup
