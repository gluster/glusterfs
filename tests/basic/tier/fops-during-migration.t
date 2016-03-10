#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


NUM_BRICKS=3
DEMOTE_FREQ=5
PROMOTE_FREQ=5

TEST_STR="Testing write and truncate fops on tier migration"


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


# Checks that the contents of the file matches the input string
#$1 : file_path
#$2 : comparison string

function check_file_content () {
        contents=`cat $1`
        echo $contents
        if [ "$contents" = "$2" ]; then
                echo "1"
        else
                echo "0"
        fi
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

$CLI volume set $V0 diagnostics.client-log-level DEBUG

TEST mkdir $M0/dir1

# Create a large file (320MB), so that rebalance takes time
# The file will be created on the hot tier

dd if=/dev/zero of=$M0/dir1/FILE1 bs=64k count=5120

# Get the path of the file on the hot tier
HPATH=`find $B0/hot/ -name  FILE1`
echo "File path on hot tier: "$HPATH


# Wait for the tier process to demote the file
EXPECT_WITHIN $REBALANCE_TIMEOUT "yes" is_sticky_set $HPATH

# Get the path of the file on the cold tier
CPATH=`find $B0/cold/ -name  FILE1`
echo "File path on cold tier: "$CPATH

# Test setxattr
TEST setfattr -n "user.test_xattr" -v "qwerty" $M0/dir1/FILE1

# Change the file contents while it is being migrated
echo $TEST_STR > $M0/dir1/FILE1

# The file contents should have changed even if the file
# is not done migrating
EXPECT "1" check_file_content $M0/dir1/FILE1 "$TEST_STR"


# Wait for the tier process to finish migrating the file
EXPECT_WITHIN $REBALANCE_TIMEOUT "no" is_sticky_set $CPATH

# The file contents should have changed
EXPECT "1" check_file_content $M0/dir1/FILE1 "$TEST_STR"


TEST getfattr -n "user.test_xattr" $M0/dir1/FILE1

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
