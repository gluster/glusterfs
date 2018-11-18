#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0};
TEST $CLI volume set $V0 leases on
TEST $CLI volume set $V0 open-behind off
TEST $CLI volume set $V0 write-behind on
TEST $CLI volume start $V0

logdir=`gluster --print-logdir`
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST mkdir $M0/test
TEST touch $M0/test/lease

build_tester $(dirname $0)/glfs-lease.c -lgfapi
build_tester $(dirname $0)/glfs-lease-recall.c -lgfapi
TEST $(dirname $0)/glfs-lease $V0 $logdir/glfs-lease.log $logdir/lease-test.log
TEST $(dirname $0)/glfs-lease-recall $V0 $logdir/glfs-lease-recall.log $logdir/lease-test-recall.log

TEST $CLI volume set $V0 leases off

cleanup_tester $(dirname $0)/glfs-lease
cleanup;
