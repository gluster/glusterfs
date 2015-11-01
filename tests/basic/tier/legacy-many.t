#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=12
MIGRATION_TIMEOUT=10
DEMOTE_FREQ=60
PROMOTE_FREQ=10
TEST_DIR="test_files"
NUM_FILES=15

function read_all {
    for file in *
    do
        cat $file
    done
}

cleanup

TEST glusterd
TEST pidof glusterd

# Create distributed replica volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
TEST $CLI volume start $V0

TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 features.ctr-enabled on

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# Create a number of "legacy" files before attaching tier
mkdir $M0/${TEST_DIR}
cd $M0/${TEST_DIR}
TEST create_many_files tfile $NUM_FILES
wait

# Attach tier
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST
TEST $CLI volume rebalance $V0 tier status


TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
TEST $CLI volume set $V0 cluster.read-freq-threshold 0
TEST $CLI volume set $V0 cluster.write-freq-threshold 0

# wait a little for lookup heal to finish
sleep 10

# Read "legacy" files
drop_cache $M0

sleep_until_mid_cycle $DEMOTE_FREQ

TEST read_all

# Test to make sure files were promoted as expected
sleep $PROMOTE_TIMEOUT
EXPECT_WITHIN $PROMOTE_TIMEOUT "0" check_counters $NUM_FILES 0

cd;
cleanup
