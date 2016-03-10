#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


NUM_BRICKS=2
DEMOTE_FREQ=15
DEMOTE_TIMEOUT=10
PROMOTE_FREQ=500


#Both src and dst files must hash to the same hot tier subvol
SRC_FILE="file1.txt"
DST_FILE="newfile1.txt"


# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{0..$1}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume start $V0
        TEST $CLI volume tier $V0 attach $H0:$B0/hot/${V0}{0..$1}
        TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
        TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
        TEST $CLI volume set $V0 cluster.tier-mode test

#We do not want any files to be promoted during this test
        TEST $CLI volume set $V0 features.record-counters on
        TEST $CLI volume set $V0 cluster.read-freq-threshold 50
        TEST $CLI volume set $V0 cluster.write-freq-threshold 50
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


# The file will be created on the hot tier

TEST touch "$M0/$SRC_FILE"

# Get the path of the file on the hot tier
HPATH=`find $B0/hot/ -name  "$SRC_FILE"`
echo "File path on hot tier: "$HPATH


EXPECT "yes" exists_and_regular_file $HPATH

# Wait for the tier process to demote the file
sleep $DEMOTE_FREQ

# Get the path of the file on the cold tier
CPATH=`find $B0/cold/ -name  "$SRC_FILE"`
echo "File path on cold tier: "$CPATH

EXPECT_WITHIN $DEMOTE_TIMEOUT "yes" exists_and_regular_file $CPATH

#We don't want $DST_FILE to get demoted
TEST $CLI volume set $V0 cluster.tier-demote-frequency $PROMOTE_FREQ

#This will be created on the hot tier

touch "$M0/$DST_FILE"
HPATH=`find $B0/hot/ -name "$DST_FILE"`
echo "File path on hot tier: "$HPATH

TEST mv $M0/$SRC_FILE $M0/$DST_FILE

# We expect a single file to exist at this point
# when viewed on the mountpoint
EXPECT 1 echo $(ls -l $M0 | grep $DST_FILE | wc -l)

cleanup;

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
