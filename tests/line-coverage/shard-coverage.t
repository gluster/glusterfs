#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick
TEST $CLI volume set $V0 features.shard on
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

# It is good to copy the file locally and build it, so the scope remains
# inside tests directory.
TEST cp $(dirname ${0})/../basic/gfapi/glfsxmp-coverage.c glfsxmp.c
TEST build_tester ./glfsxmp.c -lgfapi
TEST ./glfsxmp $V0 $H0
TEST cleanup_tester ./glfsxmp
TEST rm ./glfsxmp.c

TEST $GFS -s $H0 --volfile-id $V0 $M1;

TEST $(dirname $0)/../basic/rpc-coverage.sh $M1


TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
