#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


DEMOTE_FREQ=5
PROMOTE_FREQ=5

function create_dist_rep_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 replica 2 $H0:$B0/cold/${V0}{0..3}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume set $V0 features.ctr-enabled on
        TEST $CLI volume start $V0
}

function attach_dist_rep_tier () {
        TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/hot/${V0}{0..3}
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


#Create and start a volume
create_dist_rep_vol

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

# Create a large file (320MB), so that rebalance takes time
TEST dd if=/dev/zero of=$M0/foo bs=64k count=5120

# Get the path of the file on the cold tier
CPATH=`find $B0/cold/ -name  foo`
echo "File path on cold tier: "$CPATH

#Now attach the tier
attach_dist_rep_tier

#Write into the file to promote it
echo "good morning">>$M0/foo

# Wait for the tier process to promote the file
EXPECT_WITHIN $REBALANCE_TIMEOUT "yes" is_sticky_set $CPATH

# Get the path of the file on the hot tier
HPATH=`find $B0/hot/ -name  foo`

echo "File path on hot tier: "$HPATH
TEST rm -rf $M0/foo
TEST ! stat $HPATH
TEST ! stat $CPATH

#unlink during demotion
HPATH="";
CPATH="";

# Create a large file (320MB), so that rebalance takes time
TEST dd if=/dev/zero of=$M0/foo1 bs=64k count=5120

# Get the path of the file on the hot tier
HPATH=`find $B0/hot/ -name  foo1`
echo "File path on hot tier : "$HPATH

EXPECT_WITHIN $REBALANCE_TIMEOUT "yes" is_sticky_set $HPATH

# Get the path of the file on the cold tier
CPATH=`find $B0/cold/ -name  foo1`
echo "File path on cold tier : "$CPATH

TEST rm -rf $M0/foo1

TEST ! stat $HPATH
TEST ! stat $CPATH

cleanup;

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
