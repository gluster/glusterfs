#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 ensure-durability off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=enable

TEST gluster volume profile $V0 start
TEST dd of=$M0/a if=/dev/zero bs=1024k count=1 oflag=append
finodelk_max_latency=$($CLI volume profile $V0 info | grep FINODELK | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{7,}")

TEST [ -z $finodelk_max_latency ]

cleanup;
