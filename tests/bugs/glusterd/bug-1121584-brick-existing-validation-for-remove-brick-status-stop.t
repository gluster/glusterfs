#!/bin/bash

## Test case for BZ-1121584. Execution of remove-brick status/stop command
## should give error for brick which is not part of volume.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

cleanup;

## Start glusterd
TEST glusterd
TEST pidof glusterd

## Lets Create and start volume
TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Start remove-brick operation on the volume
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start

## By giving non existing brick for remove-brick status/stop command should
## give error.
TEST ! $CLI volume remove-brick $V0 $H0:$B0/ABCD status
TEST ! $CLI volume remove-brick $V0 $H0:$B0/ABCD stop

## By giving brick which is part of volume for remove-brick status/stop command
## should print statistics of remove-brick operation or stop remove-brick
## operation.
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 status
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 stop

cleanup;
