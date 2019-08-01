#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 arbiter 1 $H0:$B0/${V0}{1,2,3,4,5,6};

## Start volume and verify
TEST $CLI volume start $V0;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

cp $(dirname ${0})/../basic/gfapi/glfsxmp-coverage.c glfsxmp.c
build_tester ./glfsxmp.c -lgfapi
$(dirname $0)/../basic/rpc-coverage.sh $M1 >/dev/null
./glfsxmp $V0 $H0 >/dev/null

TEST cleanup_tester ./glfsxmp
TEST rm ./glfsxmp.c

## Finish up
TEST $CLI volume stop $V0;

TEST $CLI volume delete $V0;

cleanup;
