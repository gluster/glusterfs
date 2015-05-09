#!/bin/bash

## Test case for bitrot.
## volume info should not show 'features.scrub: resume' if scrub process is
## resumed from paused state.


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

## Set bitrot scrubber process to pause state
TEST $CLI volume bitrot $V0 scrub pause

## gluster volume info command should show scrub process pause.
EXPECT 'pause' volinfo_field $V0 'features.scrub';


## Resume scrub process on volume $V0
TEST $CLI volume bitrot $V0 scrub resume

## gluster volume info command should show scrub process Active
EXPECT 'Active' volinfo_field $V0 'features.scrub';


## Disable bitrot on volume $V0
TEST $CLI volume bitrot $V0 disable

## gluster volume info command should show scrub process Inactive
EXPECT 'Inactive' volinfo_field $V0 'features.scrub';


cleanup;
