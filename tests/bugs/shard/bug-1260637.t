#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

# Create a file.
TEST touch $M0/foo

# Check that the shard xattrs are set in the backend.
TEST getfattr -n trusted.glusterfs.shard.block-size $B0/${V0}0/foo
TEST getfattr -n trusted.glusterfs.shard.file-size $B0/${V0}0/foo

# Verify that shard xattrs are not exposed on the mount.
TEST ! getfattr -n trusted.glusterfs.shard.block-size $M0/foo
TEST ! getfattr -n trusted.glusterfs.shard.file-size $M0/foo

# Verify that shard xattrs cannot be set from the mount.
TEST ! setfattr -n trusted.glusterfs.shard.block-size -v "123" $M0/foo
TEST ! setfattr -n trusted.glusterfs.shard.file-size  -v "123" $M0/foo

# Verify that shard xattrs cannot be removed from the mount.
TEST ! setfattr -x trusted.glusterfs.shard.block-size $M0/foo
TEST ! setfattr -x trusted.glusterfs.shard.file-size $M0/foo

# Verify that shard xattrs are not listed when listxattr is triggered.
TEST ! "getfattr -d -m . $M0/foo | grep shard"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
