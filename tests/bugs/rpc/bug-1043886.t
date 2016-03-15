#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

## Mount volume as NFS export
TEST mount_nfs $H0:/$V0 $N0 nolock;

# just a random uid/gid
uid=22162
gid=5845

mkdir $N0/other;
chown $uid:$gid $N0/other;

TEST $CLI volume set $V0 server.root-squash on;
TEST $CLI volume set $V0 server.anonuid $uid;
TEST $CLI volume set $V0 server.anongid $gid;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

# create files and directories in the root of the glusterfs and nfs mount
# which is owned by root and hence the right behavior is getting EACCESS
# as the fops are executed as nfsnobody.
touch $M0/file 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $M0/dir 2>/dev/null;
TEST [ $? -ne 0 ]

# Here files and directories should be getting created as other directory is owned
# by tmp_user as server.anonuid and server.anongid have the value of tmp_user uid and gid
TEST touch $M0/other/file 2>/dev/null;
TEST [ "$(stat -c %u:%g $N0/other/file)" = "$uid:$gid" ];
TEST mkdir $M0/other/dir 2>/dev/null;
TEST [ "$(stat -c %u:%g $N0/other/dir)" = "$uid:$gid" ];

## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
