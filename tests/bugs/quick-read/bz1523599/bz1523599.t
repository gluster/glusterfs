#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../fileio.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/test_bz1523599.c -lgfapi -o $(dirname $0)/test_bz1523599
TEST ./$(dirname $0)/test_bz1523599 0 $H0 $V0 test_bz1523599 $logdir/bz1523599.log
TEST ./$(dirname $0)/test_bz1523599 1 $H0 $V0 test_bz1523599 $logdir/bz1523599.log
TEST ./$(dirname $0)/test_bz1523599 0 $H0 $V0 test_bz1523599 $logdir/bz1523599.log
TEST ./$(dirname $0)/test_bz1523599 2 $H0 $V0 test_bz1523599 $logdir/bz1523599.log

cleanup_tester $(dirname $0)/test_bz1523599

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;

