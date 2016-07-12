#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
cleanup;

TEST glusterd;
TEST pidof glusterd

#Tests for data-self-heal-algorithm option
function create_setup_for_self_heal {
        file=$1
        kill_brick $V0 $H0 $B0/${V0}1
        dd of=$file if=/dev/urandom bs=1024k count=1 2>&1 > /dev/null
        $CLI volume start $V0 force
}

function test_write {
        dd of=$M0/a if=/dev/urandom bs=1k count=1 2>&1 > /dev/null
}

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 client-log-level DEBUG
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0;

touch $M0/a

TEST $CLI volume set $V0 cluster.data-self-heal-algorithm full
EXPECT full volume_option $V0 cluster.data-self-heal-algorithm
create_setup_for_self_heal $M0/a
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
cat $file > /dev/null 2>&1
EXPECT_WITHIN $HEAL_TIMEOUT 0 get_pending_heal_count $V0
TEST cmp $B0/${V0}1/a $B0/${V0}2/a

TEST $CLI volume set $V0 cluster.data-self-heal-algorithm diff
EXPECT diff volume_option $V0 cluster.data-self-heal-algorithm
create_setup_for_self_heal $M0/a
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
cat $file > /dev/null 2>&1
EXPECT_WITHIN $HEAL_TIMEOUT 0 get_pending_heal_count $V0
TEST cmp $B0/${V0}1/a $B0/${V0}2/a

TEST $CLI volume reset $V0 cluster.data-self-heal-algorithm
create_setup_for_self_heal $M0/a
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
cat $file > /dev/null 2>&1
EXPECT_WITHIN $HEAL_TIMEOUT 0 get_pending_heal_count $V0
TEST cmp $B0/${V0}1/a $B0/${V0}2/a

TEST ! $CLI volume set $V0 cluster.data-self-heal-algorithm ""

cleanup;
