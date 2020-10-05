#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TESTS_EXPECTED_IN_LOOP=12

function perform_io_on_mount {
    local m="$1"
    local f="$2"
    local lockfile="$3"
    while [ -f "$m/$lockfile" ];
    do
        dd if=/dev/zero of=$m/$f bs=1M count=1
    done
}

function perform_graph_switch {
    for i in {1..3}
    do
        TEST_IN_LOOP $CLI volume set $V0 performance.stat-prefetch off
        sleep 3
        TEST_IN_LOOP $CLI volume set $V0 performance.stat-prefetch on
        sleep 3
    done
}

function count_files {
    ls $M0 | wc -l
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 flush-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/lock
for i in {1..100}; do perform_io_on_mount $M0 $i lock & done
EXPECT_WITHIN 5 "101" count_files

perform_graph_switch
TEST rm -f $M0/lock
wait
EXPECT "100" count_files
TEST rm -f $M0/{1..100}
EXPECT "0" count_files

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Repeat the tests with reader-thread-count
TEST $GFS --reader-thread-count=10 --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/lock
for i in {1..100}; do perform_io_on_mount $M0 $i lock & done
EXPECT_WITHIN 5 "101" count_files

perform_graph_switch
TEST rm -f $M0/lock
wait
EXPECT "100" count_files
TEST rm -f $M0/{1..100}
EXPECT "0" count_files

cleanup
