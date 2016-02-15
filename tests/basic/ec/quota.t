#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup
QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../quota.c -o $QDD

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse $H0:$B0/${V0}{0..2}
EXPECT 'Created' volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

TEST mkdir -p $M0/test

TEST $CLI volume quota $V0 enable

TEST $CLI volume quota $V0 limit-usage /test 10MB

EXPECT "10.0MB" quota_hard_limit "/test";
EXPECT "80%" quota_soft_limit "/test";

TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

TEST ! $QDD $M0/test/file1.txt 256 48
TEST rm $M0/test/file1.txt

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test"

TEST $QDD $M0/test/file2.txt 256 32
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "8.0MB" quotausage "/test"

TEST rm $M0/test/file2.txt
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test"
TEST $CLI volume stop $V0
EXPECT "1" get_aux

rm -f $QDD
cleanup;
