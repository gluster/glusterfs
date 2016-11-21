#!/bin/bash

. $(dirname $0)/../../include.rc
#. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..2};

TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.cache-invalidation on
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on
TEST $CLI volume set $V0 open-behind off

logdir=`gluster --print-logdir`

TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
echo "Hello" > $M0/test

TEST build_tester $(dirname $0)/bug-read-hang.c -lgfapi
TEST $(dirname $0)/bug-read-hang $H0 $V0 $logdir/bug-read-hang.log

cleanup_tester $(dirname $0)/bug-read-hang

cleanup;
