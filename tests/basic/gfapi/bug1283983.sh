#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

## Enable Upcall cache-invalidation feature
TEST $CLI volume set $V0 features.cache-invalidation on;

build_tester $(dirname $0)/bug1283983.c -lgfapi

TEST ./$(dirname $0)/bug1283983 $H0 $V0  $logdir/bug1283983.log

## There shouldn't be any NULL gfid messages logged
TEST ! cat $logdir/bug1283983.log | grep "upcall" | grep "00000000-0000-0000-0000-000000000000"

cleanup_tester $(dirname $0)/bug1283983

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
