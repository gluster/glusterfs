#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


NUM_BRICKS=3
DEMOTE_FREQ=10
PROMOTE_FREQ=10
NUM_FILES=5
TEST_DIR=test
# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{0..$1}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume start $V0
}

function create_dist_tier_vol () {
        TEST $CLI volume attach-tier $V0 $H0:$B0/hot/${V0}{0..$1}
        TEST $CLI volume set $V0 cluster.tier-mode test
        TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
        TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
        TEST $CLI volume set $V0 features.record-counters on
        TEST $CLI volume set $V0 cluster.read-freq-threshold 2
        TEST $CLI volume set $V0 cluster.write-freq-threshold 2
}

cleanup;


TEST glusterd

#Create and start a tiered volume
create_dist_vol $NUM_BRICKS

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

# create some files
mkdir $M0/$TEST_DIR
cd $M0/${TEST_DIR}

date > file1
touch file2

# attach tier
create_dist_tier_vol $NUM_BRICKS

sleep_until_mid_cycle $PROMOTE_FREQ

# check if promotion on single hit, should fail
date >> file2
cat file1
drop_cache $M0
sleep $PROMOTE_FREQ
EXPECT "0" check_counters 0 0

# check if promotion on double hit, should suceed
sleep_until_mid_cycle $PROMOTE_FREQ
date >> file2
drop_cache $M0
cat file1
date >> file2
drop_cache $M0
cat file1

EXPECT_WITHIN $PROMOTE_FREQ "0" check_counters 2 0

TEST ! $CLI volume set $V0 features.record-counters off

cd /

cleanup

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
