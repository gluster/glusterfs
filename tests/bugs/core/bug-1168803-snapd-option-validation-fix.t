#!/bin/bash

## Test case for BZ-1168803 - snapd option validation should not fail if the
#snapd is not running

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 features.uss enable

## Now set another volume option, this should not fail
TEST $CLI volume set $V0 performance.io-cache off

## start the volume
TEST $CLI volume start $V0

## Kill snapd daemon and then try to stop the volume which should not fail
kill $(ps aux | grep glusterfsd | grep snapd | awk '{print $2}')

TEST $CLI volume stop $V0

cleanup;
