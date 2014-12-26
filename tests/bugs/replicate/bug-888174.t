#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This tests if flush, fsync wakes up the delayed post-op or not.
#If it is not woken up, INODELK from the next command waits
#for post-op-delay secs. There would be pending changelog even after the command
#completes.

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/r2_0 $H0:$B0/r2_1

TEST $CLI volume set $V0 cluster.eager-lock on

TEST $CLI volume set $V0 performance.flush-behind off
EXPECT "off" volume_option $V0 performance.flush-behind

TEST $CLI volume set $V0 cluster.post-op-delay-secs 3
EXPECT "3" volume_option $V0 cluster.post-op-delay-secs

TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0

#Check that INODELK MAX latency is not in the order of seconds
TEST gluster volume profile $V0 start
for i in {1..5}
do
        echo hi > $M0/a
done
#Test if the MAX INODELK fop latency is of the order of seconds.
inodelk_max_latency=$($CLI volume profile $V0 info | grep INODELK | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{7,}")

TEST [ -z $inodelk_max_latency ]

TEST dd of=$M0/a if=/dev/urandom bs=1024k count=10 conv=fsync
#Check for no trace of pending changelog. Flush should make sure of it.
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/r2_0/a trusted.afr.dirty
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/r2_1/a trusted.afr.dirty


dd of=$M0/a if=/dev/urandom bs=1024k count=1024 2>/dev/null &
p=$!
#trigger graph switches, tests for fsync not leaving any pending flags
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off

kill -TERM $p
#wait for dd to exit
wait  > /dev/null 2>&1

#Goal is to check if there is permanent FOOL changelog
sleep 5
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/r2_0/a trusted.afr.dirty
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/r2_1/a trusted.afr.dirty

cleanup;
