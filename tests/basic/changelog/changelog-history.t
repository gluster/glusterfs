#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../env.rc

cleanup;

HISTORY_BIN_PATH=$(dirname $0)/../../utils/changelog
build_tester $HISTORY_BIN_PATH/get-history.c -lgfchangelog

time_before_enable1=$(date '+%s')
CHANGELOG_PATH_0="$B0/${V0}0/.glusterfs/changelogs"
ROLLOVER_TIME=2

TEST glusterd
TEST pidof glusterd

sleep 3
time_before_enable2=$(date '+%s')

sleep 3
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $CLI volume start $V0

sleep 3
time_after_enable1=$(date '+%s')

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
touch $M0/file{1..10}

sleep 3
time_after_enable2=$(date '+%s')

let time_future=time_after_enable2+600

#Fails as start falls before changelog enable
EXPECT "-3" $HISTORY_BIN_PATH/get-history $time_before_enable1 $time_before_enable2

#Fails as start falls before changelog enable
EXPECT "-3" $HISTORY_BIN_PATH/get-history $time_before_enable2 $time_after_enable1

#Passes as start and end falls in same htime file
EXPECT "0" $HISTORY_BIN_PATH/get-history $time_after_enable1 $time_after_enable2

#Passes, gives the changelogs till continuous changelogs are available
# but returns 1
EXPECT "1" $HISTORY_BIN_PATH/get-history $time_after_enable2 $time_future

#Disable and enable changelog
TEST $CLI volume set $V0 changelog.changelog off
sleep 6
time_between_htime=$(date '+%s')
sleep 6
TEST $CLI volume set $V0 changelog.changelog on

sleep 6
touch $M0/test{1..10}
time_in_sec_htime1=$(date '+%s')

sleep 6
touch $M0/test1{1..10}
time_in_sec_htime2=$(date '+%s')

sleep 3
TEST $CLI volume set $V0 changelog.changelog off
sleep 3
time_after_disable=$(date '+%s')

#Passes, gives the changelogs till continuous changelogs are available
# but returns 1
EXPECT "1" $HISTORY_BIN_PATH/get-history $time_after_enable1 $time_in_sec_htime2

#Fails as start falls between htime files
EXPECT "-3" $HISTORY_BIN_PATH/get-history $time_between_htime $time_in_sec_htime1

#Passes as start and end falls in same htime file
EXPECT "0" $HISTORY_BIN_PATH/get-history $time_in_sec_htime1 $time_in_sec_htime2

#Passes, gives the changelogs till continuous changelogs are available
EXPECT "0" $HISTORY_BIN_PATH/get-history $time_in_sec_htime2 $time_after_disable

TEST rm $HISTORY_BIN_PATH/get-history

cleanup;
