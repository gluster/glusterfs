#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

function usage()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | \
                grep "$QUOTA_PATH" | awk '{print $4}'
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 network.inode-lru-limit 1

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available
TEST mount_nfs $H0:/$V0 $N0
deep=/0/1/2/3/4/5/6/7/8/9
TEST mkdir -p $N0/$deep

TEST dd if=/dev/zero of=$N0/$deep/file bs=1k count=10240

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 20MB
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

TEST dd if=/dev/zero of=$N0/$deep/newfile_1 bs=512 count=10240
# wait for write behind to complete.
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "15.0MB" usage "/"

# compile the test write program and run it
TEST $CC $(dirname $0)/quota-anon-fd-nfs.c -o $(dirname $0)/quota-anon-fd-nfs;
# Try to create a 100Mb file which should fail
TEST ! $(dirname $0)/quota-anon-fd-nfs $N0/$deep/newfile_2 "104857600"
TEST rm -f $N0/$deep/newfile_2

## Before killing daemon to avoid deadlocks
umount_nfs $N0

cleanup;
