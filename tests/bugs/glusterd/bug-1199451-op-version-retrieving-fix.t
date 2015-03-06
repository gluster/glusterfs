#!/bin/bash

## Test case for BZ-1199451 (gluster command should retrieve current op-version
## of the NODE)

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start glusterd
TEST glusterd
TEST pidof glusterd

## Lets create and start volume
TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

## glusterd command should retrieve current op-version of the node
TEST $CLI volume get $V0 cluster.op-version

cleanup;
