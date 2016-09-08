#!/bin/bash

#Testcase:
#On brick restart, new HTIME.TSTAMP file should not be created.
#But on changelog disable/enable HTIME.TSTAMP should be created.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../changelog.rc
cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0"1";

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 changelog.changelog on;
##Let changelog init complete before killing gluster processes
sleep 1

TEST killall_gluster;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "0" online_brick_count

TEST glusterd;
TEST pidof glusterd;

##Let the brick processes starts
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count

##On brick restart only one HTIME should be found.
EXPECT 1 count_htime_files;

##On changelog disable/enable, new HTIME should be created.
TEST $CLI volume set $V0 changelog.changelog off;
TEST $CLI volume set $V0 changelog.changelog on;
EXPECT 2 count_htime_files;

cleanup;
