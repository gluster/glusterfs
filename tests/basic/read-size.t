#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function clear_stats {
        > /var/lib/glusterfs/stats/glusterfs_d_backends_${V0}*.dump
}

function got_expected_read_count {
        expected_size=$1
        expected_value=$2
        grep -h aggr.read_${expected_size} /var/lib/glusterd/stats/glusterfsd__d_backends_${V0}*.dump \
                | cut -d':' -f2 \
                | grep "\"$expected_value\""
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
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 diagnostics.latency-measurement on
TEST $CLI volume set $V0 diagnostics.count-fop-hits on
TEST $CLI volume set $V0 diagnostics.stats-dump-interval 2
TEST $CLI volume set $V0 performance.io-cache on
TEST $CLI volume set $V0 performance.nfs.io-cache on
TEST $CLI volume set $V0 performance.io-cache.read-size 512KB
EXPECT '512KB' volinfo_field $V0 'performance.io-cache.read-size'
TEST $CLI volume set $V0 performance.io-cache.min-cached-read-size 32KB
EXPECT '32KB' volinfo_field $V0 'performance.io-cache.min-cached-read-size'

TEST $CLI volume start $V0

sleep 2;

TEST mount.nfs -overs=3,noacl,nolock,noatime $HOSTNAME:/$V0 $N0

# First read of big file should not be cached
TEST dd if=/dev/zero of=$N0/100mb_file bs=1M count=100 oflag=sync
TEST cat $N0/100mb_file
EXPECT_WITHIN 3 "Y" got_expected_read_count "512kb" 200

# The number of reads should stay the same from the previous cat since they're cached
TEST cat $N0/100mb_file
EXPECT_WITHIN 3 "Y" got_expected_read_count "512kb" 200

# Should not be cached
TEST dd if=/dev/zero of=$N0/10kb_file bs=1K count=10 oflag=sync
TEST cat $N0/10kb_file
EXPECT_WITHIN 3 "Y" got_expected_read_count "8kb" 1

# The reads should increment indicating they are not being cached
TEST cat $N0/10kb_file
EXPECT_WITHIN 3 "Y" got_expected_read_count "8kb" 2

cleanup;
