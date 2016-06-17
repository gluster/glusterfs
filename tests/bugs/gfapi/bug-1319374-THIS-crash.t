#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 diagnostics.client-log-flush-timeout 30

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/bug-1319374.c -lgfapi
TEST $(dirname $0)/bug-1319374 $H0 $V0 $logdir/bug-1319374.log

cleanup_tester $(dirname $0)/bug-1319374

cleanup;
