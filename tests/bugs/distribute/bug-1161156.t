#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

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

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4,5,6};
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

# Testing with NFS for no particular reason
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available
TEST mount_nfs $H0:/$V0 $N0
mydir="dir"
TEST mkdir -p $N0/$mydir
TEST mkdir -p $N0/newdir

TEST dd if=/dev/zero of=$N0/$mydir/file bs=1k count=10240

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 20MB
TEST $CLI volume quota $V0 limit-usage /newdir 5MB
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

TEST dd if=/dev/zero of=$N0/$mydir/newfile_1 bs=512 count=10240
# wait for write behind to complete.
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "15.0MB" usage "/"
TEST ! dd if=/dev/zero of=$N0/$mydir/newfile_2 bs=1k count=10240 conv=fdatasync

# Test rename within a directory. It should pass even when the
# corresponding directory quota is filled.
TEST mv $N0/dir/file $N0/dir/newfile_3

# rename should fail here with disk quota exceeded
TEST ! mv $N0/dir/newfile_3 $N0/newdir/

umount_nfs $N0
TEST $CLI volume stop $V0
EXPECT "1" get_aux
cleanup;
