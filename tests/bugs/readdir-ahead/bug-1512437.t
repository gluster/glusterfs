#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

TEST $CLI volume set $V0 parallel-readdir on
TEST $CLI volume set $V0 readdir-optimize on

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
TEST mkdir -p $M0/subdir1/subdir2;
umount $M0
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
count=`ls -1 $M0/subdir1 | wc -l`
TEST [ $count -eq 1 ]

cleanup;
