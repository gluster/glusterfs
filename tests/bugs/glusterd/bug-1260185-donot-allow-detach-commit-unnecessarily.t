#!/bin/bash

## Test case for BZ: 1260185
## Do not allow detach-tier commit without "force" option or without
## user have not started "detach-tier start" operation

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

## Perform attach-tier operation on volume $V0
TEST $CLI volume tier $V0 attach $H0:$B0/${V0}{3..4}

## detach-tier commit operation without force option on volume $V0
## should not succeed
TEST ! $CLI --mode=script volume tier $V0 detach commit

## detach-tier commit operation with force option on volume $V0
## should succeed
TEST  $CLI volume tier $V0 detach force

sleep 3

## Again performing attach-tier operation on volume $V0
TEST $CLI volume tier $V0 attach $H0:$B0/${V0}{5..6}

## Do detach-tier start on volume $V0
TEST $CLI volume tier $V0 detach start

## Now detach-tier commit on volume $V0 should succeed.
TEST $CLI volume tier $V0 detach commit

cleanup;
