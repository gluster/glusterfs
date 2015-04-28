#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Create a volume
TEST glusterd;
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1};
EXPECT 'Created' volinfo_field $V0 'Status';

## Volume start
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Enable a protected and a resettable/unprotected option
TEST $CLI volume quota $V0 enable
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG

## Reset cmd resets only unprotected option(s), succeeds.
TEST $CLI volume reset $V0;

## Set an unprotected option
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG

## Now 1 protected and 1 unprotected options are set
## Reset force should succeed
TEST $CLI volume reset $V0 force;

TEST $CLI volume stop $V0
EXPECT "1" get_aux
TEST $CLI volume delete $V0

cleanup;
