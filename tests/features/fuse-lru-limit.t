#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "2" online_brick_count

EXPECT "1" get_mount_active_size_value $V0 $M0
EXPECT "0" get_mount_lru_size_value $V0 $M0

mkdir ${M0}/dir-{1..9}
for i in {1..9}; do
    for j in {1..1000}; do
        echo "Test file" > ${M0}/dir-$i/file-$j;
    done;
done
lc=$(get_mount_lru_size_value $V0 ${M0})
# ideally it should be 9000+
TEST [ $lc -ge 9000 ]

TEST umount $M0

TEST glusterfs -s $H0 --volfile-id $V0 --lru-limit 1000 $M0

TEST find $M0
lc=$(get_mount_lru_size_value $V0 ${M0})
# ideally it should be <1000
# Not sure if there are any possibilities of buffer need.
TEST [ $lc -le 1000 ]

TEST rm -rf $M0/*

EXPECT "1" get_mount_active_size_value $V0 $M0
EXPECT "0" get_mount_lru_size_value $V0 $M0

cleanup
