#!/bin/bash

. $(dirname $0)/../include.rc

# Because I have added 10 iterations of rpc-coverage and glfsxmp for errorgen
SCRIPT_TIMEOUT=600

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6};

TEST $CLI volume set $V0 error-gen posix;
TEST $CLI volume set $V0 debug.error-failure 3%;

## Start volume and verify
TEST $CLI volume start $V0;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

cp $(dirname ${0})/../basic/gfapi/glfsxmp-coverage.c glfsxmp.c
build_tester ./glfsxmp.c -lgfapi
for i in $(seq 1 10); do
    # as there is error-gen, there can be errors, so no
    # need to test for success of below two commands
    $(dirname $0)/../basic/rpc-coverage.sh $M1 >/dev/null
    ./glfsxmp $V0 $H0 >/dev/null
done

TEST cleanup_tester ./glfsxmp
TEST rm ./glfsxmp.c

## Finish up
TEST $CLI volume stop $V0;

TEST $CLI volume delete $V0;

cleanup;
