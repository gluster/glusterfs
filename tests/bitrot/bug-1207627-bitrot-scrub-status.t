#!/bin/bash

## Test case for bitrot scrub status BZ:1207627

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create and start the volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2}
TEST $CLI volume start $V0

## Enable bitrot for volume $V0
TEST $CLI volume bitrot $V0 enable

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

## Setting scrubber frequency daily
TEST $CLI volume bitrot $V0 scrub-frequency hourly

## Setting scrubber throttle value lazy
TEST $CLI volume bitrot $V0 scrub-throttle lazy


EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Active' scrub_status $V0 'State of scrub'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'lazy'   scrub_status $V0 'Scrub impact'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'hourly' scrub_status $V0 'Scrub frequency'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '/var/log/glusterfs/bitd.log' scrub_status $V0 'Bitrot error log location'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '/var/log/glusterfs/scrub.log' scrub_status $V0 'Scrubber error log location'

cleanup;
