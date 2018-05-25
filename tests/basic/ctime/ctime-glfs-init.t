#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 3 ${H0}:$B0/brick{1,2,3};
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 ctime on
TEST $CLI volume set $V0 utime on
TEST $CLI volume start $V0;

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/ctime-glfs-init.c -lgfapi -lpthread

TEST ./$(dirname $0)/ctime-glfs-init ${H0} $V0 $logdir/ctime-glfs-init.log

cleanup_tester $(dirname $0)/ctime-glfs-init

cleanup;

