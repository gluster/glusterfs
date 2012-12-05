#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd;
TEST pidof glusterd

#Tests for data-self-heal-algorithm option
function create_setup_for_self_heal {
        file=$1
        kill_brick $V0 $H0 $B0/${V0}1
        dd of=$file if=/dev/urandom bs=1M count=1 2>&1 > /dev/null
        $CLI volume start $V0 force
}

function test_write {
        dd of=$M0/a if=/dev/urandom bs=1k count=1 2>&1 > /dev/null
}

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 client-log-level DEBUG
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0;

touch $M0/a

TEST $CLI volume set $V0 cluster.data-self-heal-algorithm full
EXPECT full volume_option $V0 cluster.data-self-heal-algorithm
create_setup_for_self_heal $M0/a
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
ls -l $file 2>&1 > /dev/null
TEST cmp $B0/${V0}1/a $B0/${V0}2/a

TEST $CLI volume set $V0 cluster.data-self-heal-algorithm diff
EXPECT diff volume_option $V0 cluster.data-self-heal-algorithm
create_setup_for_self_heal $M0/a
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
ls -l $file 2>&1 > /dev/null
TEST cmp $B0/${V0}1/a $B0/${V0}2/a

TEST $CLI volume reset $V0 cluster.data-self-heal-algorithm
create_setup_for_self_heal $M0/a
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
ls -l $file 2>&1 > /dev/null
TEST cmp $B0/${V0}1/a $B0/${V0}2/a

TEST ! $CLI volume set $V0 cluster.data-self-heal-algorithm ""

#Tests for quorum-type option
TEST ! $CLI volume set $V0 cluster.quorum-type ""
TEST $CLI volume set $V0 cluster.quorum-type fixed
EXPECT fixed volume_option $V0 cluster.quorum-type
TEST $CLI volume set $V0 cluster.quorum-count 2
kill_brick $V0 $H0 $B0/${V0}1
TEST ! test_write
TEST $CLI volume set $V0 cluster.quorum-type auto
EXPECT auto volume_option $V0 cluster.quorum-type
TEST ! test_write
TEST $CLI volume set $V0 cluster.quorum-type none
EXPECT none volume_option $V0 cluster.quorum-type
TEST test_write
TEST $CLI volume reset $V0 cluster.quorum-type
TEST test_write
cleanup;
