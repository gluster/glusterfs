#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;
CURDIR=`pwd`

TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
TEST $CLI snapshot config activate-on-create enable
TEST $CLI volume set $V0 features.uss enable

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock
TEST mkdir $N0/testdir

TEST $CLI snapshot create snap1 $V0 no-timestamp
TEST $CLI snapshot create snap2 $V0 no-timestamp

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/testdir/.snaps

TEST cd $N0/testdir
TEST cd .snaps
TEST ls

TEST $CLI snapshot deactivate snap2
TEST ls

TEST cd $CURDIR

#Clean up
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0
TEST $CLI snapshot delete snap1
TEST $CLI snapshot delete snap2
TEST $CLI volume stop $V0 force
TEST $CLI volume delete $V0

cleanup;

