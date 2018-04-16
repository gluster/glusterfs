#!/bin/bash
#Tests that sequential write workload doesn't lead to FSYNCs

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST truncate -s 128M $B0/xfs_image
TEST mkfs.xfs -f $B0/xfs_image
TEST mkdir $B0/bricks
TEST mount -t xfs -o loop $B0/xfs_image $B0/bricks

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/bricks/brick{0,1,3}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

# Write 50MB of data, which will try to consume 50x3=150MB on $B0/bricks.
# Before that, we hit ENOSPC in pre-op cbk, which should not crash the mount.
TEST ! dd if=/dev/zero of=$M0/a bs=1M count=50
TEST stat $M0/a
cleanup;
