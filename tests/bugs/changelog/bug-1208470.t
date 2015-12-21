#!/bin/bash

## Testcase:
## Avoid creating any EMPTY changelog(over the changelog rollover time)

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../changelog.rc
cleanup;

## override current changelog rollover-time
## to avoid sleeping for long duration.
CL_RO_TIME=5

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0"1";

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Set changelog ON
TEST $CLI volume set $V0 changelog.changelog on;

EXPECT 1 count_changelog_files $B0/${V0}1

## Set changelog rollover time
TEST $CLI volume set $V0 changelog.rollover-time $CL_RO_TIME;

## Wait for changelog rollover time
sleep $CL_RO_TIME

## NO additional empty changelogs created
EXPECT 1 count_changelog_files $B0/${V0}1
