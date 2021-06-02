#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This tests checks if the file migration performs fsync based on option

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
for i in {1..100};
do
    echo abc > $M0/$i
done

TEST $CLI volume profile $V0 start
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}1
TEST $CLI volume rebalance $V0 start force;
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
fsync_count=$($CLI volume profile $V0 info incremental | grep -w FSYNC | wc -l)
EXPECT_NOT "^0$" echo $fsync_count
EXPECT_NOT "^0$" rebalanced_files_field $V0

TEST $CLI volume set $V0 rebalance.ensure-durability off
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}2
TEST $CLI volume rebalance $V0 start force;
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
fsync_count=$($CLI volume profile $V0 info incremental | grep -w FSYNC | wc -l)
EXPECT "^0$" echo $fsync_count
EXPECT_NOT "^0$" rebalanced_files_field $V0

cleanup
