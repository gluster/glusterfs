#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This test tests that an extra fd_unref does not happen in rebalance
#migration completion check code path in dht

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST touch $M0/1
#This rename creates a link file for 10 in the other volume.
TEST mv $M0/1 $M0/10
#Lets keep writing to the file which will trigger rebalance completion check
dd if=/dev/zero of=$M0/10 bs=1k &
bg_pid=$!
#Now rebalance force will migrate file '10'
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
#If the bug exists mount would have crashed by now
TEST ls $M0
kill -9 $bg_pid > /dev/null 2>&1
wait > /dev/null 2>&1
cleanup
