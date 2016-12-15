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

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 'N' check_if_snapd_exist

glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_if_snapd_exist

cleanup;
