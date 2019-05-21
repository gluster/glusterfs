#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../env.rc

cleanup;

HISTORY_BIN_PATH=$(dirname $0)/../../utils/changelog
build_tester $HISTORY_BIN_PATH/test-history-api.c -lgfchangelog

CHANGELOG_PATH_0="$B0/${V0}0/.glusterfs/changelogs"
ROLLOVER_TIME=2

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $CLI volume start $V0

sleep 3
start=$(date '+%s')

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
touch $M0/file{1..10}

for i in {1..12};do echo "data" > $M0/file$i; sleep 1;done
end=$(date '+%s')
sleep 2

#Passes as start and end falls in same htime file
EXPECT "0" $HISTORY_BIN_PATH/test-history-api $start $end

#Wait for changelogs to be in .processed directory
sleep 2

EXPECT "Y" processed_changelogs "/tmp/scratch_v1/.history/.processed"
TEST rm $HISTORY_BIN_PATH/test-history-api
rm -rf /tmp/scratch_v1

cleanup;
