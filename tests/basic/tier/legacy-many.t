#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5
MIGRATION_TIMEOUT=10
DEMOTE_FREQ=60
PROMOTE_FREQ=4
TEST_DIR="test_files"
NUM_FILES=20


# Grab md5sum without file path (failed attempt notifications are discarded)
function fingerprint {
    md5sum $1 2> /dev/null | grep --only-matching -m 1 '^[0-9a-f]*'
}

# Create a large number of files. Store their md5 signatures.
function create_many_files {
    mkdir ${TEST_DIR}
    for i in `seq 1 $NUM_FILES`; do
        dd if=/dev/urandom of=./${TEST_DIR}/i$i bs=1048576 count=1;
        id[i]=$(fingerprint "./${TEST_DIR}/i$i");
    done
}

function confirm_tier_removed {
    $CLI system getspec $V0 | grep $1
    if [ $? == 0 ]; then
        echo "1"
    else
        echo "0"
    fi
}

function confirm_vol_stopped {
    $CLI volume stop $1
    if [ $? == 0 ]; then
        echo "0"
    else
        echo "1"
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
cd $M0
TEST create_many_files
wait

# Attach tier
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST
TEST $CLI volume rebalance $V0 tier status

TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
TEST $CLI volume set $V0 cluster.read-freq-threshold 0
TEST $CLI volume set $V0 cluster.write-freq-threshold 0

# Read "legacy" files
drop_cache $M0
cd ${TEST_DIR}
TEST read_all

# Test to make sure files were promoted as expected
sleep $DEMOTE_TIMEOUT
EXPECT_WITHIN $DEMOTE_TIMEOUT "0" check_counters 20 0

cd;
cleanup
