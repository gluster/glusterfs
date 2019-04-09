#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 3 ${H0}:$B0/brick{1,2,3};
TEST $CLI volume set $V0 performance.stat-prefetch on
TEST $CLI volume set $V0 performance.readdir-ahead off
TEST $CLI volume start $V0;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

TEST mkdir $M0/dir0
TEST "echo hello_world > $M0/dir0/FILE"

ctime1=$(stat -c %Z $M0/dir0/FILE)
echo "Mount change time: $ctime1"

sleep 2

#Write to back end directly to modify ctime of backend file
TEST "echo write_from_backend >> $B0/brick1/dir0/FILE"
TEST "echo write_from_backend >> $B0/brick2/dir0/FILE"
TEST "echo write_from_backend >> $B0/brick3/dir0/FILE"
echo "Backend change time"
echo "brick1: $(stat -c %Z $B0/brick1/dir0/FILE)"
echo "brick2: $(stat -c %Z $B0/brick2/dir0/FILE)"
echo "brick3: $(stat -c %Z $B0/brick3/dir0/FILE)"

#Stop and start to hit the case of no inode for readdir
TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

TEST build_tester $(dirname $0)/ctime-readdir.c

#Do readdir
TEST ./$(dirname $0)/ctime-readdir $M0/dir0

EXPECT "$ctime1" stat -c %Z $M0/dir0/FILE
echo "Mount change time after readdir $(stat -c %Z $M0/dir0/FILE)"

cleanup_tester $(dirname $0)/ctime-readdir

cleanup;
