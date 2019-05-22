#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../env.rc

cleanup;

CHANGELOG_BIN_PATH=$(dirname $0)/../../utils/changelog
build_tester $CHANGELOG_BIN_PATH/test-changelog-api.c -lgfchangelog

CHANGELOG_PATH_0="$B0/${V0}0/.glusterfs/changelogs"
ROLLOVER_TIME=2

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

sleep 3;

#Listen to changelog journal notifcations
$CHANGELOG_BIN_PATH/test-changelog-api &
for i in {1..12};do echo "data" > $M0/file$i 2>/dev/null; sleep 1;done &

#Wait for changelogs to be in .processed directory
sleep 12

EXPECT "Y" processed_changelogs "/tmp/scratch_v1/.processed"
TEST rm $CHANGELOG_BIN_PATH/test-changelog-api
rm -rf /tmp/scratch_v1

cleanup;
