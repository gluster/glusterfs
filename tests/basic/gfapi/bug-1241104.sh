#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 localhost:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

## Enable Upcall cache-invalidation feature
TEST $CLI volume set $V0 features.cache-invalidation on;

build_tester $(dirname $0)/bug-1241104.c -lgfapi -o $(dirname $0)/bug-1241104

TEST ./$(dirname $0)/bug-1241104 $V0  $logdir/bug-1241104.log

cleanup_tester $(dirname $0)/bug1241104

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
