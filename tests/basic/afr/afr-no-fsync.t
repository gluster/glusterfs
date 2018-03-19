#!/bin/bash
#Tests that sequential write workload doesn't lead to FSYNCs

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0,1,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST dd if=/dev/zero of=$M0/a bs=1M count=500
TEST ! "$CLI volume profile $V0 info incremental | grep FSYNC"

cleanup;
