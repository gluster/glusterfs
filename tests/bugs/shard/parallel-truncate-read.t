#!/bin/bash

#This test will crash if shard's LRU contains a shard's inode even after the
#inode is forgotten. Minimum time for crash to happen I saw was 180 seconds

. $(dirname $0)/../../include.rc

function keep_writing {
        cd $M0;
        while [ -f /tmp/parallel-truncate-read ]
        do
                dd if=/dev/zero of=file1 bs=1M count=16
        done
        cd
}

function keep_reading {
        cd $M0;
        while [ -f /tmp/parallel-truncate-read ]
        do
                cat file1 > /dev/null
        done
        cd
}

cleanup;

TEST touch /tmp/parallel-truncate-read
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
keep_writing &
keep_reading &
sleep 180
TEST rm -f /tmp/parallel-truncate-read
wait
#test that the mount is operational
TEST stat $M0

cleanup;
