#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc

NUM_BRICKS=3
DEMOTE_FREQ=5
DEMOTE_TIMEOUT=10
PROMOTE_FREQ=5

FILE="file1.txt"
FILE_LINK="file2.txt"

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
        TEST $CLI volume set $V0 cluster.read-freq-threshold 4
        TEST $CLI volume set $V0 cluster.write-freq-threshold 4
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
touch "$M0/$FILE"

# Get the path of the file on the hot tier
HPATH=`find $B0/hot/ -name  "$FILE"`
echo "File path on hot tier: "$HPATH

# Expecting the file to be on the hot tier
EXPECT "yes" exists_and_regular_file $HPATH

sleep_until_mid_cycle $DEMOTE_FREQ

# Try to heat the file using 5 metadata operations
# WITHOUT setting ctr-record-metadata-heat on
touch "$M0/$FILE"
chmod +x "$M0/$FILE"
chown root "$M0/$FILE"
ln "$M0/$FILE" "$M0/$FILE_LINK"
rm -rf "$M0/$FILE_LINK"

# Wait for the tier process to demote the file
sleep $DEMOTE_TIMEOUT

# Get the path of the file on the cold tier
CPATH=`find $B0/cold/ -name  "$FILE"`
echo "File path on cold tier: "$CPATH

# Expecting the file to be on cold tier
EXPECT "yes" exists_and_regular_file $CPATH

#Set ctr-record-metadata-heat on
TEST $CLI volume set $V0 ctr-record-metadata-heat on

sleep_until_mid_cycle $DEMOTE_FREQ

# Heating the file using 5 metadata operations
touch "$M0/$FILE"
chmod +x "$M0/$FILE"
chown root "$M0/$FILE"
ln "$M0/$FILE" "$M0/$FILE_LINK"
rm -rf "$M0/$FILE_LINK"

# Wait for the tier process to demote the file
sleep $DEMOTE_TIMEOUT

# Get the path of the file on the hot tier
echo "File path on hot tier: "$HPATH

# Expecting the file to be on the hot tier
EXPECT "yes" exists_and_regular_file $HPATH

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
