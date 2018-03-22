#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

function reads_brick_count {
        $CLI volume profile $V0 info incremental | grep -w READ | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0..2}

TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0

# Disable all caching
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
TEST dd if=/dev/urandom of=$M0/FILE bs=1M count=8
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# TEST if the option gives the intended behavior. The way we perform this test
# is by performing reads from the mount and write to /dev/null. If the
# read-hash-mode is 3, then for a given file, more than 1 brick should serve the
# read-fops where as with the default read-hash-mode (i.e. 1), only 1 brick will.

# read-hash-mode=1
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT "1" mount_get_option_value $M0 $V0-replicate-0 read-hash-mode
TEST $CLI volume profile $V0 start
TEST dd if=$M0/FILE of=/dev/null bs=1M
count=`reads_brick_count`
TEST [ $count -eq 1 ]
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# read-hash-mode=3
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
TEST $CLI volume set $V0 cluster.read-hash-mode 3
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "3" mount_get_option_value $M0 $V0-replicate-0 read-hash-mode
TEST $CLI volume profile $V0 info clear
TEST dd if=$M0/FILE of=/dev/null bs=1M
count=`reads_brick_count`
TEST [ $count -eq 2 ]

# Check that the arbiter did not serve any reads
arbiter_reads=$($CLI volume top $V0 read brick $H0:$B0/${V0}2|grep FILE|awk '{print $1}')
TEST [ -z $arbiter_reads ]

cleanup;
