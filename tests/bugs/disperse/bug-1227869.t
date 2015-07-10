#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 $H0:$B0/${V0}{1..3}
EXPECT "Created" volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 100MB

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST $QDD $M0/file 256 40

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quotausage "/"

EXPECT "0" echo $(df -k $M0 | grep -q '10240 '; echo $?)
EXPECT "0" echo $(df -k $M0 | grep -q '92160 '; echo $?)

rm -f $QDD
cleanup
