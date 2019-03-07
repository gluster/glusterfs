#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

CHANGELOG_PATH_0="$B0/${V0}2/.glusterfs/changelogs"
ROLLOVER_TIME=100

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST dd if=/dev/zero of=$M0/file1 bs=128K count=5

TEST $CLI volume profile $V0 start
TEST $CLI volume add-brick $V0 replica 3 arbiter 1 $H0:$B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST $CLI volume profile $V0 info
truncate_count=$($CLI volume profile $V0 info | grep TRUNCATE | awk '{count += $8} END {print count}')

EXPECT "1" echo $truncate_count
EXPECT "1" check_changelog_op ${CHANGELOG_PATH_0} "^ D "

cleanup;
