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

function tier_status () {
        $CLI volume tier $V0 status | grep "success" | wc -l
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
TEST create_many_files file $NUM_FILES
wait

# Attach tier
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

TEST $CLI volume set $V0 cluster.tier-mode test
TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
TEST $CLI volume set $V0 cluster.read-freq-threshold 0
TEST $CLI volume set $V0 cluster.write-freq-threshold 0

# wait a little for lookup heal to finish
wait_for_tier_start

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" tier_status

# make sure fix layout completed
CPATH=$B0/${V0}0
echo $CPATH > /tmp/out
TEST getfattr -n "trusted.tier.fix.layout.complete" $CPATH

# Read "legacy" files
drop_cache $M0

sleep_until_mid_cycle $DEMOTE_FREQ

TEST read_all

# Test to make sure files were promoted as expected
sleep $PROMOTE_TIMEOUT
EXPECT_WITHIN $PROMOTE_TIMEOUT "0" check_counters $NUM_FILES 0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" detach_start $V0
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0 $H0:$B0/${V0}${CACHE_BRICK_FIRST}"

TEST $CLI volume tier $V0 detach commit

# fix layout flag should be cleared
TEST ! getfattr -n "trusted.tier.fix.layout.complete" $CPATH

cd;
cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
