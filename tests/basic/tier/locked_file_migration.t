#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc


NUM_BRICKS=3
DEMOTE_FREQ=7
PROMOTE_FREQ=30
DEMOTE_TIMEOUT=15

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

function check_counters {
    index=0
    ret=0
    rm -f /tmp/tc*.txt
    echo "0" > /tmp/tc2.txt
    $CLI volume rebalance $V0 tier status | grep localhost > /tmp/tc.txt

    promote=`cat /tmp/tc.txt |awk '{print $2}'`
    demote=`cat /tmp/tc.txt |awk '{print $3}'`
    if [ "${promote}" != "${1}" ]; then
         echo "1" > /tmp/tc2.txt

    elif [ "${demote}" != "${2}" ]; then
         echo "2" > /tmp/tc2.txt
    fi

    # temporarily disable non-Linux tests.
    case $OSTYPE in
        NetBSD | FreeBSD | Darwin)
            echo "0" > /tmp/tc2.txt
            ;;
    esac
    cat /tmp/tc2.txt
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

#We don't want promotes to happen in this test
        TEST $CLI volume set $V0 cluster.read-freq-threshold 10
        TEST $CLI volume set $V0 cluster.write-freq-threshold 10
}


cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info


# Create and start a tiered volume
create_dist_tier_vol $NUM_BRICKS

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST mkdir $M0/dir1
build_tester $(dirname $0)/file_lock.c -o file_lock
cp $(dirname $0)/file_lock $M0/file_lock

# The files will be created on the hot tier
touch $M0/dir1/FILE1
touch $M0/dir1/FILE2

# For FILE1, take a POSIX write lock on the entire file.
# Don't take a lock on FILE2

./file_lock $M0/dir1/FILE1 W &

sleep $DEMOTE_FREQ

# Wait for the tier process to demote the file
# Only FILE2 and file_lock should be demoted
# FILE1 should be skipped because of the lock held
# on it

EXPECT_WITHIN $DEMOTE_TIMEOUT "0" check_counters 0 2

sleep 10

rm $(dirname $0)/file_lock

cleanup;

