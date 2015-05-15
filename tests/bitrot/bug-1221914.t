#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

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

## Turn off write-behind (write-behind clubs writes together)
TEST $CLI volume set $V0 performance.write-behind off

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
$PYTHON -c "import os,stat; os.mknod('${fname}', stat.S_IFREG | stat.S_IRUSR | stat.S_IWUSR | stat.S_IROTH | stat.S_IRGRP)"

backpath=$(get_backend_paths $fname)

TEST $STUB_EXEC $fname $(dirname $backpath)

rm -f $fname;

##cleanups..
rm -f $STUB_EXEC

cleanup;
