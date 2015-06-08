#!/bin/bash

## Test case for bitrot BZ:1229134
## gluster volume set <VOLNAME> bitrot * command succeeds,
## which is not supported to enable bitrot.

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

## 'gluster volume set <VOLNAME>' command for bitrot should failed.
TEST ! $CLI volume set $V0 bitrot enable
TEST ! $CLI volume set $V0 bitrot disable
TEST ! $CLI volume set $V0 scrub-frequency daily
TEST ! $CLI volume set $V0 scrub pause
TEST ! $CLI volume set $V0 scrub-throttle lazy


## 'gluster volume bitrot <VOLNAME> *' command for bitrot should succeeds.
TEST $CLI volume bitrot $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

TEST $CLI volume bitrot $V0 scrub pause
TEST $CLI volume bitrot $V0 scrub-frequency daily
TEST $CLI volume bitrot $V0 scrub-throttle lazy

cleanup;

