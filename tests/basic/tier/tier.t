#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5
MIGRATION_TIMEOUT=10
DEMOTE_FREQ=4
PROMOTE_FREQ=4


# Timing adjustment to avoid spurious errors with first instances of file_on_fast_tier
function sleep_first_cycle {
    startTime=$(date +%s)
    mod=$(( ( $startTime % $DEMOTE_FREQ ) + 1 ))
    sleep $mod
}

# Grab md5sum without file path (failed attempt notifications are discarded)
function fingerprint {
    md5sum $1 2> /dev/null | grep --only-matching -m 1 '^[0-9a-f]*'
}

function file_on_slow_tier {
    found=0

    for i in `seq 0 $LAST_BRICK`; do
        test -e $B0/${V0}${i}/$1 && found=1 && break;
    done

    if [ "$found" == "1" ]
    then
        slow_hash1=$2
        slow_hash2=$(fingerprint "$B0/${V0}${i}/$1")

        if [ "$slow_hash1" == "$slow_hash2" ]
            then
                echo "0"
            else
                echo "2"
        fi
    else
        echo "1"
    fi
}

function file_on_fast_tier {
    found=0

    for j in `seq $CACHE_BRICK_FIRST $CACHE_BRICK_LAST`; do
        test -e $B0/${V0}${j}/$1 && found=1 && break;
    done


    if [ "$found" == "1" ]
    then
        fast_hash1=$2
        fast_hash2=$(fingerprint "$B0/${V0}${j}/$1")

        if [ "$fast_hash1" == "$fast_hash2" ]
            then
                echo "0"
            else
                echo "2"
        fi
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

function check_counters {
    index=0
    ret=0
    rm -f /tmp/tc*.txt
    echo "0" > /tmp/tc2.txt

    $CLI volume rebalance $V0 tier status | grep localhost > /tmp/tc.txt

    cat /tmp/tc.txt | grep -o '[0-9*]' | while read line; do
        if [ $index == 0 ]; then
            index=1
            test $line -ne $1 && echo "1" > /tmp/tc2.txt
        else
            test $line -ne $2 && echo "2" > /tmp/tc2.txt
        fi
    done

    cat /tmp/tc2.txt
}

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
sleep_first_cycle
$CLI volume rebalance $V0 tier status

#Tier options expect non-negative value
TEST ! $CLI volume set $V0 cluster.tier-promote-frequency -1

#Tier options expect non-negative value
TEST ! $CLI volume set $V0 cluster.read-freq-threshold qwerty

TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
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
mkdir /tmp/d1

# Create a file. It should be on the fast tier.
uuidgen > /tmp/d1/data.txt
md5data=$(fingerprint /tmp/d1/data.txt)
mv /tmp/d1/data.txt ./d1/data.txt

TEST file_on_fast_tier d1/data.txt $md5data

uuidgen > /tmp/d1/data2.txt
md5data2=$(fingerprint /tmp/d1/data2.txt)
cp /tmp/d1/data2.txt ./d1/data2.txt

uuidgen > /tmp/d1/data3.txt
md5data3=$(fingerprint /tmp/d1/data3.txt)
mv /tmp/d1/data3.txt ./d1/data3.txt

# Check auto-demotion on write new.
sleep $DEMOTE_TIMEOUT

# Check auto-promotion on write append.
UUID=$(uuidgen)
echo $UUID >> /tmp/d1/data2.txt
md5data2=$(fingerprint /tmp/d1/data2.txt)
echo $UUID >> ./d1/data2.txt

# Check promotion on read to slow tier
drop_cache $M0
cat d1/data3.txt

sleep $PROMOTE_TIMEOUT
sleep $DEMOTE_FREQ
EXPECT "0" check_counters 2 6

# stop gluster, when it comes back info file should have tiered volume
killall glusterd
TEST glusterd

EXPECT "0" file_on_slow_tier d1/data.txt $md5data
EXPECT "0" file_on_slow_tier d1/data2.txt $md5data2
EXPECT "0" file_on_slow_tier d1/data3.txt $md5data3

TEST $CLI volume detach-tier $V0 start

TEST $CLI volume detach-tier $V0 commit force

EXPECT "0" confirm_tier_removed ${V0}${CACHE_BRICK_FIRST}

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" confirm_vol_stopped $V0

cd;

cleanup
rm -rf /tmp/d1


