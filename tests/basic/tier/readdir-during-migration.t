#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


NUM_BRICKS=3
DEMOTE_FREQ=5
PROMOTE_FREQ=5
NUM_FILES=30
TEST_DIR=test
# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{0..$1}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume start $V0
        TEST $CLI volume attach-tier $V0 $H0:$B0/hot/${V0}{0..$1}
        TEST $CLI volume set $V0 cluster.tier-mode test
        TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
        TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
        TEST $CLI volume set $V0 cluster.read-freq-threshold 0
        TEST $CLI volume set $V0 cluster.write-freq-threshold 0
}

function check_file_count() {
    if [ $(ls -1 | wc -l) == $1 ]; then
        echo "1"
    else
        echo "0"
    fi
}

cleanup;


TEST glusterd

#Create and start a tiered volume
create_dist_tier_vol $NUM_BRICKS

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

# Create a number of "legacy" files before attaching tier
mkdir $M0/${TEST_DIR}
cd $M0/${TEST_DIR}
TEST create_many_files tfile $NUM_FILES

EXPECT "1" check_file_count $NUM_FILES

sleep $DEMOTE_FREQ

EXPECT "1" check_file_count $NUM_FILES

cd /

cleanup;

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
