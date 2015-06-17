#!/bin/bash

## Test case for bitrot BZ:1210684
## Bitrot scrub pause/resume option should give proper error if scrubber is
## already pause/resume and admin try to perform same operation on a volume


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

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

## Pause scrubber operation on volume $V0
TEST $CLI volume bitrot $V0 scrub pause

## Pausing scrubber again should not success and should give error
TEST ! $CLI volume bitrot $V0 scrub pause

## Resume scrubber operation on volume $V0
TEST $CLI volume bitrot $V0 scrub resume

## Resuming scrubber again should not success and should give error
TEST ! $CLI volume bitrot $V0 scrub resume

cleanup;
