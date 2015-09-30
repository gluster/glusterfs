#!/bin/bash

## Test case for BZ-1140160  Volume option set <vol> <file-snapshot> and
## <features.encryption> <value> command input should validate correctly.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create and start volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0

## Set features.file-snapshot and features.encryption option with non-boolean
## value. These options should fail.
TEST ! $CLI volume set $V0 features.file-snapshot abcd
TEST ! $CLI volume set $V0 features.encryption redhat

## Set other options with valid value. These options should succeed.
TEST $CLI volume set $V0 barrier enable
TEST $CLI volume set $V0 ping-timeout 60

## Set features.file-snapshot and features.encryption option with valid boolean
## value. These options should succeed.
TEST $CLI volume set $V0 features.file-snapshot on
TEST $CLI volume set $V0 features.encryption on

cleanup;
