#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

#Tests that local structures in afr are removed from granted/blocked list of
#locks when inodelk fails on all bricks

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3}
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.client-io-threads off
TEST $CLI volume set $V0 delay-gen locks
TEST $CLI volume set $V0 delay-gen.delay-duration 5000000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable finodelk

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST touch $M0/file
#Trigger write and stop bricks so inodelks fail on all bricks leading to
#lock failure condition
echo abc >> $M0/file &

TEST $CLI volume stop $V0
TEST $CLI volume reset $V0 delay-gen
wait
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_meta $M0 $V0-replicate-0 2
#Test that only one write succeeded, this tests that delay-gen worked as
#expected
echo abc >> $M0/file
EXPECT "abc" cat $M0/file

cleanup;
