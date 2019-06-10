#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3  $H0:$B0/${V0}{1,2,3,4,5,6};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '6' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

file="/var/lib/glusterd/vols/$V0/trusted-$V0.tcp-fuse.vol"
sed -i -e 's$send-gids true$send-gids true\n    option testing.old-protocol true$g' $file

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

## TODO: best way to increase coverage is to have a gfapi program
## which covers maximum fops
TEST $(dirname $0)/../basic/rpc-coverage.sh $M1

TEST cp $(dirname ${0})/../basic/gfapi/glfsxmp-coverage.c glfsxmp.c
TEST build_tester ./glfsxmp.c -lgfapi
TEST ./glfsxmp $V0 $H0
TEST cleanup_tester ./glfsxmp
TEST rm ./glfsxmp.c

cleanup;
