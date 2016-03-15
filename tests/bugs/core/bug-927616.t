#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 performance.open-behind off;
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock;

TEST mkdir $M0/dir;

mkdir $M0/other;
cp /etc/passwd $M0/;
cp $M0/passwd $M0/file;
chmod 600 $M0/file;

chown -R nfsnobody:nfsnobody $M0/dir;

TEST $CLI volume set $V0 server.root-squash on;

sleep 1;

# tests should fail.
touch $M0/foo 2>/dev/null;
TEST [ $? -ne 0 ]
touch $N0/foo 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $M0/new 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $N0/new 2>/dev/null;
TEST [ $? -ne 0 ]

TEST $CLI volume set $V0 server.root-squash off;

sleep 1;

# tests should pass.
touch $M0/foo 2>/dev/null;
TEST [ $? -eq 0 ]
touch $N0/bar 2>/dev/null;
TEST [ $? -eq 0 ]
mkdir $M0/new 2>/dev/null;
TEST [ $? -eq 0 ]
mkdir $N0/old 2>/dev/null;
TEST [ $? -eq 0 ]

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
