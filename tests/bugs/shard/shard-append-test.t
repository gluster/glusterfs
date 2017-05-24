#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 3 ${H0}:$B0/brick{1,2,3};
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off

#Uncomment the following line after shard-queuing is implemented
#TEST $CLI volume set $V0 performance.write-behind off

TEST $CLI volume set $V0 performance.strict-o-direct on
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0;

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/shard-append-test.c -lgfapi -lpthread

TEST ./$(dirname $0)/shard-append-test ${H0} $V0 $logdir/shard-append-test.log

cleanup_tester $(dirname $0)/shard-append-test

cleanup;
