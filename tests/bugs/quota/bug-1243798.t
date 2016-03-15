#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 noac,nolock

TEST mkdir -p $N0/dir1/dir2
TEST touch $N0/dir1/dir2/file

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 limit-objects /dir1 10

TEST stat $N0/dir1/dir2/file

sleep 2

#Remove size and contri xattr from /dir1
#Remove contri xattr from /dir1/dir2
setfattr -x trusted.glusterfs.quota.size.1 $B0/$V0/dir1
setfattr -x trusted.glusterfs.quota.00000000-0000-0000-0000-000000000001.contri.1 $B0/$V0/dir1
contri=$(getfattr -d -m . -e hex $B0/$V0/dir1/dir2 | grep contri | awk -F= '{print $1}')
setfattr -x $contri $B0/$V0/dir1/dir2

#Initiate healing by writing to a file
echo Hello > $N0/dir1/dir2/file

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "2" quota_object_list_field "/dir1" 5

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;
