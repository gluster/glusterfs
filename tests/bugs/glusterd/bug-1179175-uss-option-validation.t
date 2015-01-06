#!/bin/bash

## Test case for option features.uss validation.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create and start volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0

## Set features.uss option with non-boolean value. These non-boolean value
## for features.uss option should fail.
TEST ! $CLI volume set $V0 features.uss abcd
TEST ! $CLI volume set $V0 features.uss #$#$
TEST ! $CLI volume set $V0 features.uss 2324

## Setting other options with valid value. These options should succeed.
TEST $CLI volume set $V0 barrier enable
TEST $CLI volume set $V0 ping-timeout 60

## Set features.uss option with valid boolean value. It should succeed.
TEST  $CLI volume set $V0 features.uss enable
TEST  $CLI volume set $V0 features.uss disable


## Setting other options with valid value. These options should succeed.
TEST $CLI volume set $V0 barrier enable
TEST $CLI volume set $V0 ping-timeout 60

cleanup;
