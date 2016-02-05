#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Test that for files in metadata-split-brain, we do not wind even a single read.
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}

TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST chmod 700 $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST chmod 777 $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST umount $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

lines=`cat $M0/file|wc|awk '{print $1}'`
EXPECT 0  echo $lines
TEST umount $M0
cleanup
