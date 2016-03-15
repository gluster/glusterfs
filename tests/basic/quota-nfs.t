#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;
QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/quota.c -o $QDD

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume set $V0 network.inode-lru-limit 1
TEST $CLI volume set $V0 performance.nfs.write-behind off

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available
TEST mount_nfs $H0:/$V0 $N0
deep=/0/1/2/3/4/5/6/7/8/9
TEST mkdir -p $N0/$deep

TEST $QDD $N0/$deep/file 256 40

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 20MB
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

TEST $QDD $N0/$deep/newfile_1 256 20

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/$deep/file
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/$deep/newfile_1

#Unmount and mount to flush the data
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0
TEST mount_nfs $H0:/$V0 $N0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/$deep/file
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $N0/$deep/newfile_1

# wait for write behind to complete.
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "15.0MB" quotausage "/"

# Try to create a 100Mb file which should fail
TEST ! $QDD $N0/$deep/newfile_2 256 400
TEST rm -f $N0/$deep/newfile_2

## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST $CLI volume stop $V0
EXPECT "1" get_aux

rm -f $QDD
cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
