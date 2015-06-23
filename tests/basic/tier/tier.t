#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5

function file_on_slow_tier {
    s=$(md5sum $1)
    for i in `seq 0 $LAST_BRICK`; do
        test -e $B0/${V0}${i}/$1 && break;
    done
    if [ $? -eq 0 ] && ! [ "`md5sum $B0/${V0}${i}/$1`" == "$s" ]; then
        echo "0"
    else
        echo "1"
    fi
}

function file_on_fast_tier {
    local ret="1"

    s1=$(md5sum $1)
    s2=$(md5sum $B0/${V0}${CACHE_BRICK_FIRST}/$1)

    if [ -e $B0/${V0}${CACHE_BRICK_FIRST}/$1 ] && ! [ "$s1" == "$s2" ]; then
        echo "0"
    else
        echo "1"
    fi
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

function check_counters_nonzero {
    $CLI volume rebalance $V0 tier status | grep ' 0 '
    if [ $? == 0 ]; then
        echo "1"
    else
        echo "0"
    fi
}

DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5
MIGRATION_TIMEOUT=10
cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
# testing bug 1215122, ie should fail if replica count and bricks are not compatible.

TEST ! $CLI volume attach-tier $V0 replica 5 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

TEST $CLI volume start $V0

# The following two commands instigate a graph switch. Do them
# before attaching the tier. If done on a tiered volume the rebalance
# daemon will terminate and must be restarted manually.
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off

TEST $CLI volume set $V0 features.ctr-enabled on

#Not a tier volume
TEST ! $CLI volume set $V0 cluster.tier-demote-frequency 4

#testing bug #1228112, glusterd crashed when trying to detach-tier commit force on a non-tiered volume.
TEST ! $CLI volume detach-tier $V0 commit force

TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

$CLI volume rebalance $V0 tier status

#Tier options expect non-negative value
TEST ! $CLI volume set $V0 cluster.tier-promote-frequency -1

#Tier options expect non-negative value
TEST ! $CLI volume set $V0 cluster.read-freq-threshold qwerty

TEST $CLI volume set $V0 cluster.tier-demote-frequency 4
TEST $CLI volume set $V0 cluster.tier-promote-frequency 4
TEST $CLI volume set $V0 cluster.read-freq-threshold 0
TEST $CLI volume set $V0 cluster.write-freq-threshold 0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# Basic operations.
cd $M0
TEST stat .
TEST mkdir d1
TEST [ -d d1 ]
TEST touch d1/file1
TEST mkdir d1/d2
TEST [ -d d1/d2 ]
TEST find d1

# Create a file. It should be on the fast tier.
uuidgen > d1/data.txt
TEST file_on_fast_tier d1/data.txt

# Check manual demotion.
#TEST setfattr -n trusted.distribute.migrate-data d1/data.txt
#TEST file_on_slow_tier d1/data.txt

uuidgen > d1/data2.txt
uuidgen > d1/data3.txt
EXPECT "0" file_on_fast_tier d1/data2.txt
EXPECT "0" file_on_fast_tier d1/data3.txt

# Check auto-demotion on write new.
EXPECT_WITHIN $DEMOTE_TIMEOUT "0" file_on_slow_tier d1/data2.txt
EXPECT_WITHIN $DEMOTE_TIMEOUT "0" file_on_slow_tier d1/data3.txt
sleep 12
# Check auto-promotion on write append.
uuidgen >> d1/data2.txt

# Check promotion on read to slow tier
drop_cache $M0
cat d1/data3.txt
sleep 5
EXPECT_WITHIN $PROMOTE_TIMEOUT "0" file_on_fast_tier d1/data2.txt
EXPECT_WITHIN $PROMOTE_TIMEOUT "0" file_on_fast_tier d1/data3.txt

EXPECT "0" check_counters_nonzero

# stop gluster, when it comes back info file should have tiered volume
killall glusterd
TEST glusterd

TEST $CLI volume detach-tier $V0 start

TEST $CLI volume detach-tier $V0 commit force

EXPECT "0" file_on_slow_tier d1/data.txt

EXPECT "0" confirm_tier_removed ${V0}${CACHE_BRICK_FIRST}

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" confirm_vol_stopped $V0

cd;

cleanup
