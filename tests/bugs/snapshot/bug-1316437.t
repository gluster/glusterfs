#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST glusterd

# Intentionally not carving lvms for this as we will not be taking
# snapshots in this testcase
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6};

TEST $CLI volume start $V0;

TEST $CLI volume set $V0 features.uss enable;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

killall glusterd glusterfsd glusterfs

SNAPD_PID=$(ps auxww | grep snapd | grep -v grep | awk '{print $2}');
TEST ! [ $SNAPD_PID -gt 0 ];

glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

cleanup;
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1359717
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1359717
