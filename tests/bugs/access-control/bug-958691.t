#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock;

# Tests for the fuse mount
TEST mkdir $M0/dir;
TEST chmod 1777 $M0/dir;
TEST touch $M0/dir/file{1,2};

TEST $CLI volume set $V0 server.root-squash enable;

mv $M0/dir/file1 $M0/dir/file11 2>/dev/null;
TEST [ $? -ne 0 ];

TEST $CLI volume set $V0 server.root-squash disable;
TEST rm -rf $M0/dir;

sleep 1;

# tests for nfs mount
TEST mkdir $N0/dir;
TEST chmod 1777 $N0/dir;
TEST touch $N0/dir/file{1,2};

TEST $CLI volume set $V0 server.root-squash enable;

mv $N0/dir/file1 $N0/dir/file11 2>/dev/null;
TEST [ $? -ne 0 ];

TEST $CLI volume set $V0 server.root-squash disable;
TEST rm -rf $N0/dir;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
