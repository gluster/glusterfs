#!/bin/bash

## Test case for cluster.min-free-disk option validation.


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start glusterd
TEST glusterd
TEST pidof glusterd

## Lets create and start volume
TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

## Setting invalid value for option cluster.min-free-disk should fail
TEST ! $CLI volume set $V0 min-free-disk ""
TEST ! $CLI volume set $V0 min-free-disk 143.!/12
TEST ! $CLI volume set $V0 min-free-disk 123%
TEST ! $CLI volume set $V0 min-free-disk 194.34%

## Setting fractional value as a size (unit is byte) for option
## cluster.min-free-disk should fail
TEST ! $CLI volume set $V0 min-free-disk 199.051
TEST ! $CLI volume set $V0 min-free-disk 111.999

## Setting valid value for option cluster.min-free-disk should pass
TEST  $CLI volume set $V0 min-free-disk 12%
TEST  $CLI volume set $V0 min-free-disk 56.7%
TEST  $CLI volume set $V0 min-free-disk 120
TEST  $CLI volume set $V0 min-free-disk 369.0000


cleanup;
