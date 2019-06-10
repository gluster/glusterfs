#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
cleanup;

TEST glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5,6,7,8}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 cluster.read-subvolume-index 0
TEST $CLI volume set $V0 features.tag-namespaces on
TEST $CLI volume start $V0
TEST $CLI volume set $V0 storage.build-pgfid on

sleep 2

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;


mkdir -p $M1/namespace

# subvol_1 = bar, subvol_2 = foo, subvol_3 = hey
# Test create, write (tagged by loc, fd respectively).
touch $M1/namespace/{bar,foo,hey}

open $M1/namespace/hey

## TODO: best way to increase coverage is to have a gfapi program
## which covers maximum fops
TEST $(dirname $0)/../basic/rpc-coverage.sh $M1

TEST cp $(dirname ${0})/../basic/gfapi/glfsxmp-coverage.c glfsxmp.c
TEST build_tester ./glfsxmp.c -lgfapi
TEST ./glfsxmp $V0 $H0
TEST cleanup_tester ./glfsxmp
TEST rm ./glfsxmp.c

cleanup;
