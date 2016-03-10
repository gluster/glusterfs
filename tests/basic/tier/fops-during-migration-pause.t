#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc

NUM_BRICKS=3
DEMOTE_FREQ=10
PROMOTE_FREQ=10

TEST_STR="Testing write and truncate fops on tier migration"

function is_sticky_set () {
        echo $1
        if [ -k $1 ];
        then
                echo "yes"
        else
                echo "no"
        fi
}


# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{0..$1}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume set $V0 features.ctr-enabled on
        TEST $CLI volume start $V0
        TEST $CLI volume attach-tier $V0 $H0:$B0/hot/${V0}{0..$1}
        TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
        TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
        TEST $CLI volume set $V0 cluster.read-freq-threshold 0
        TEST $CLI volume set $V0 cluster.write-freq-threshold 0
        TEST $CLI volume set $V0 cluster.tier-mode test
}


cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info


#Create and start a tiered volume
create_dist_tier_vol $NUM_BRICKS

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST mkdir $M0/dir1

# Create a large file (800MB), so that rebalance takes time
# The file will be created on the hot tier
sleep_until_mid_cycle $DEMOTE_FREQ
dd if=/dev/zero of=$M0/dir1/FILE1 bs=256k count=5120

# Get the path of the file on the hot tier
HPATH=`find $B0/hot/ -name  FILE1`
echo "File path on hot tier: "$HPATH


# Wait for the tier process to demote the file
EXPECT_WITHIN $REBALANCE_TIMEOUT "yes" is_sticky_set $HPATH

TEST $CLI volume set $V0 cluster.tier-pause on

# Wait for the tier process to finish migrating the file
EXPECT_WITHIN $REBALANCE_TIMEOUT "no" is_sticky_set $HPATH

# Get the path of the file on the cold tier
CPATH=`find $B0/cold/ -name  FILE1`

# make sure destination is empty
TEST ! test -s $CPATH

# make sure source exists and not empty
TEST test -s $HPATH

cleanup;

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
