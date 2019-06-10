#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3  $H0:$B0/${V0}{1,2,3,4,5,6,7,8,9};
TEST $CLI volume set $V0 features.cloudsync enable;
TEST $CLI volume start $V0;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

# This test covers lookup, mkdir, mknod, symlink, link, rename,
# create operations
TEST $(dirname $0)/rpc-coverage.sh $M1


TEST cp $(dirname ${0})/gfapi/glfsxmp-coverage.c glfsxmp.c
TEST build_tester ./glfsxmp.c -lgfapi
./glfsxmp $V0 $H0
cleanup_tester ./glfsxmp
rm ./glfsxmp.c

cleanup;
