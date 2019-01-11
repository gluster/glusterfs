#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

#G_TESTDEF_TEST_STATUS_CENTOS6=NFS_TEST

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0;

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock;

# random uid/gid
uid=22162
gid=5845

TEST $CLI volume set $V0 server.anonuid $uid;
TEST $CLI volume set $V0 server.anongid $gid;

# Ensure server.all-squash is disabled
TEST $CLI volume set $V0 server.all-squash disable;

# Tests for the fuse mount
mkdir $M0/other;
chown $uid:$gid $M0/other;

TEST $CLI volume set $V0 server.all-squash enable;

touch $M0/file 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $M0/dir 2>/dev/null;
TEST [ $? -ne 0 ]

TEST touch $M0/other/file 2>/dev/null;
TEST [ "$(stat -c %u:%g $M0/other/file)" = "$uid:$gid" ];
TEST mkdir $M0/other/dir 2>/dev/null;
TEST [ "$(stat -c %u:%g $M0/other/dir)" = "$uid:$gid" ];

TEST $CLI volume set $V0 server.all-squash disable;
TEST rm -rf $M0/other;

sleep 1;

# tests for nfs mount
mkdir $N0/other;
chown $uid:$gid $N0/other;

TEST $CLI volume set $V0 server.all-squash enable;

touch $N0/file 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $N0/dir 2>/dev/null;
TEST [ $? -ne 0 ]

TEST touch $N0/other/file 2>/dev/null;
TEST [ "$(stat -c %u:%g $N0/other/file)" = "$uid:$gid" ];
TEST mkdir $N0/other/dir 2>/dev/null;
TEST [ "$(stat -c %u:%g $N0/other/dir)" = "$uid:$gid" ];

TEST $CLI volume set $V0 server.all-squash disable;
TEST rm -rf $N0/other;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
