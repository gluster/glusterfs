#!/bin/bash

## Test case for bitrot
## On restarting glusterd should not reset bitrot tunable value to default


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;


## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create and start the volume
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

## Enable bitrot on volume $V0
TEST $CLI volume bitrot $V0 enable

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

## Set bitrot scrub-throttle value to lazy
TEST $CLI volume bitrot $V0 scrub-throttle lazy

## Set bitrot scrub-frequency value to monthly
TEST $CLI volume bitrot $V0 scrub-frequency monthly

## Set bitrot scrubber to pause state
TEST $CLI volume bitrot $V0 scrub pause

## restart glusterd process
pkill glusterd;
TEST glusterd;
TEST pidof glusterd;

## All the bitrot scrub tunable value should come back again.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status';
EXPECT 'lazy' volinfo_field $V0 'features.scrub-throttle';
EXPECT 'monthly' volinfo_field $V0 'features.scrub-freq';
EXPECT 'pause' volinfo_field $V0 'features.scrub';
EXPECT 'on' volinfo_field $V0 'features.bitrot';

cleanup;
