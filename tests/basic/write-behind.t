#!/bin/bash
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function clear_stats {
        > /var/lib/glusterfs/stats/glusterfs_d_backends_${V0}0.dump
}

function got_expected_write_count {
        expected_size=$1
        expected_value=$2
        grep aggr.write_${expected_size} "/var/lib/glusterd/stats/glusterfsd__d_backends_${V0}0.dump" | grep $expected_value
        if [ $? == 0 ]; then
          echo "Y";
        else
          echo "N";
        fi
}

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}

# These are needed for our tracking of write sizes
TEST $CLI volume set $V0 diagnostics.latency-measurement on
TEST $CLI volume set $V0 diagnostics.count-fop-hits on
TEST $CLI volume set $V0 diagnostics.stats-dump-interval 2

# Disable this in testing to get deterministic results
TEST $CLI volume set $V0 performance.write-behind-trickling-writes off

TEST $CLI volume start $V0

sleep 2;

TEST glusterfs -s $H0 --volfile-id $V0 $M0

# Write a 100MB file with a window-size 1MB, we should get 100 writes of 1MB each
TEST dd if=/dev/zero of=$M0/100mb_file bs=1M count=100
EXPECT_WITHIN 5 "Y" got_expected_write_count "1mb" 100

TEST $CLI volume set $V0 performance.write-behind-window-size 512KB

# Write a 100MB file with a window-size 512KB, we should get 200 writes of 512KB each
TEST dd if=/dev/zero of=$M0/100mb_file_2 bs=1M count=100
EXPECT_WITHIN 5 "Y" got_expected_write_count "512kb" 200

cleanup;
