#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc

LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5
MIGRATION_TIMEOUT=10
DEMOTE_FREQ=4
PROMOTE_FREQ=12

function file_on_slow_tier {
    found=0

    for i in `seq 0 $LAST_BRICK`; do
        test -e "$B0/${V0}${i}/$1" && found=1 && break;
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

    # temporarily disable non-Linux tests.
    case $OSTYPE in
        NetBSD | FreeBSD | Darwin)
            echo "0"
            ;;
    esac
}

function file_on_fast_tier {
    found=0

    for j in `seq $CACHE_BRICK_FIRST $CACHE_BRICK_LAST`; do
        test -e "$B0/${V0}${j}/$1" && found=1 && break;
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


cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
# testing bug 1215122, ie should fail if replica count and bricks are not compatible.

TEST ! $CLI volume tier $V0 attach replica 5 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

TEST $CLI volume start $V0

# The following two commands instigate a graph switch. Do them
# before attaching the tier. If done on a tiered volume the rebalance
# daemon will terminate and must be restarted manually.
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off

#Not a tier volume
TEST ! $CLI volume set $V0 cluster.tier-demote-frequency 4

#testing bug #1228112, glusterd crashed when trying to detach-tier commit force on a non-tiered volume.
TEST ! $CLI volume tier $V0 detach commit force

TEST $CLI volume tier $V0 attach replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST

TEST $CLI volume set $V0 cluster.tier-mode test

# create a file, make sure it can be deleted after attach tier.
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
cd $M0
TEST touch delete_me.txt
TEST rm -f delete_me.txt

# confirm watermark CLI works
TEST $CLI volume set $V0 cluster.watermark-hi 85
TEST $CLI volume set $V0 cluster.watermark-low 75
TEST $CLI volume set $V0 cluster.tier-max-mb 1000
TEST $CLI volume set $V0 cluster.tier-max-files 1000
TEST $CLI volume set $V0 cluster.tier-max-promote-file-size 1000
TEST ! $CLI volume set $V0 cluster.tier-max-files -3
TEST ! $CLI volume set $V0 cluster.watermark-low 90
TEST ! $CLI volume set $V0 cluster.read-freq-threshold -12
TEST ! $CLI volume set $V0 cluster.write-freq-threshold -12


# stop the volume and restart it. The rebalance daemon should restart.
cd /tmp
umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume start $V0

wait_for_tier_start

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
cd $M0

sleep_first_cycle $DEMOTE_FREQ
$CLI volume tier $V0 status

#Tier options expect non-negative value
TEST ! $CLI volume set $V0 cluster.tier-promote-frequency -1

#Tier options expect non-negative value
TEST ! $CLI volume set $V0 cluster.read-freq-threshold qwerty


TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
TEST $CLI volume set $V0 cluster.read-freq-threshold 0
TEST $CLI volume set $V0 cluster.write-freq-threshold 0

# Basic operations.
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

#File with spaces and special characters.
SPACE_FILE="file with spaces & $peci@l ch@r@cter$ @!@$%^$#@^^*&%$#$%.txt"

uuidgen > "/tmp/d1/$SPACE_FILE"
md5space=$(fingerprint "/tmp/d1/$SPACE_FILE")
mv "/tmp/d1/$SPACE_FILE" "./d1/$SPACE_FILE"

# Check auto-demotion on write new.
sleep $DEMOTE_TIMEOUT

# Check auto-promotion on write append.
UUID=$(uuidgen)
echo $UUID >> /tmp/d1/data2.txt
md5data2=$(fingerprint /tmp/d1/data2.txt)

sleep_until_mid_cycle $DEMOTE_FREQ
drop_cache $M0

echo $UUID >> ./d1/data2.txt
cat "./d1/$SPACE_FILE"

sleep $PROMOTE_TIMEOUT
sleep $DEMOTE_FREQ
EXPECT_WITHIN $DEMOTE_TIMEOUT "0" check_counters 2 6

# stop gluster, when it comes back info file should have tiered volume
killall glusterd
TEST glusterd

EXPECT "0" file_on_slow_tier d1/data.txt $md5data
EXPECT "0" file_on_slow_tier d1/data2.txt $md5data2
EXPECT "0" file_on_slow_tier "./d1/$SPACE_FILE" $md5space

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" detach_start $V0
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0 $H0:$B0/${V0}${CACHE_BRICK_FIRST}"

TEST $CLI volume tier $V0 detach commit

EXPECT "0" confirm_tier_removed ${V0}${CACHE_BRICK_FIRST}

confirm_vol_stopped $V0

cd;

cleanup
rm -rf /tmp/d1


#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
