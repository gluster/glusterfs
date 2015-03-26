#!/bin/bash

#Test case: This test checks when a volume is made read-only though volume set
#           and bricks are not restarted no write operations can be performed on
#           this volume

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

#Create a volume
TEST $CLI volume create $V0 $H0:$B0/${V0}0;
TEST $CLI volume start $V0

# Mount FUSE and create file/directory, create should succeed as the read-only
# is off by default
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/zerobytefile1.txt
TEST mkdir $M0/test_dir1
TEST dd if=/dev/zero of=$M0/file1 bs=1024 count=1024

# turn on read-only option through volume set
TEST gluster volume set $V0 read-only on

# worm feature can't be enabled if read-only is enabled
TEST ! gluster volume set $V0 worm on

# turn off read-only option through volume set
TEST gluster volume set $V0 read-only off

# turn on worm option through volume set
TEST gluster volume set $V0 worm on

# read-only feature can't be enabled if worm is enabled
TEST ! gluster volume set $V0 read-only on


TEST gluster volume set $V0 worm off
TEST gluster volume set $V0 read-only on

# Check whether read-operations can be performed or not
TEST cat $M0/file1

# All write operations should fail now
TEST ! touch $M0/zerobytefile2.txt
TEST ! mkdir $M0/test_dir2
TEST ! dd if=/dev/zero of=$M0/file2 bs=1024 count=1024

# turn off read-only option through volume set
TEST gluster volume set $V0 read-only off

# All write operations should succeed now
TEST touch $M0/zerobytefile2.txt
TEST mkdir $M0/test_dir2
TEST dd if=/dev/zero of=$M0/file2 bs=1024 count=1024

# Turn on worm
TEST gluster volume set $V0 worm on

# unlink should fail now
TEST ! rm -rf $M0/zerobytefile2.txt

cleanup;
