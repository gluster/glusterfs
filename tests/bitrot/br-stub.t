#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

STUB_SOURCE=$(dirname $0)/br-stub.c
STUB_EXEC=$(dirname $0)/br-stub

cleanup;

TEST glusterd
TEST pidof glusterd

## Create a distribute volume (B=2)
TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '2' brick_count $V0

## Turn off open-behind (stub does not work with anonfd yet..)
TEST $CLI volume set $V0 performance.open-behind off
EXPECT 'off' volinfo_field $V0 'performance.open-behind'

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

## Build stub C source
build_tester $STUB_SOURCE -o $STUB_EXEC -I$(dirname $0)/../../xlators/features/bit-rot/src/stub
TEST [ -e $STUB_EXEC ]

## create & check version
fname="$M0/filezero"
touch $fname
backpath=$(get_backend_paths $fname)
TEST $STUB_EXEC $fname $(dirname $backpath)

## cleanups..
rm -f $STUB_EXEC

cleanup;
