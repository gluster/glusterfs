#!/bin/bash
#This test tests that self-heals don't perform fsync when durability is turned
#off

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST $CLI volume set $V0 cluster.ensure-durability off
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST kill_brick $V0 $H0 $B0/brick0
TEST dd of=$M0/a.txt if=/dev/zero bs=1024k count=1
#NetBSD sends FSYNC so the counts go for a toss. Stop and start the volume.
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
EXPECT "^0$" echo $($CLI volume profile $V0 info | grep -w FSYNC | wc -l)

#Test that fsyncs happen when durability is on
TEST $CLI volume set $V0 cluster.ensure-durability on
TEST $CLI volume set $V0 performance.strict-write-ordering on
TEST kill_brick $V0 $H0 $B0/brick0
TEST dd of=$M0/a.txt if=/dev/zero bs=1024k count=1
#NetBSD sends FSYNC so the counts go for a toss. Stop and start the volume.
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
EXPECT "^2$" echo $($CLI volume profile $V0 info | grep -w FSYNC | wc -l)

cleanup;
