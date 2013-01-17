#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=enable

T=$(date +%s)
TEST dd of=$M0/a if=/dev/zero bs=1M count=1 oflag=append
T1=$(date +%s)
EXPECT "0" echo "$(($T1-$T))"

cleanup;
