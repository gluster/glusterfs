#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 md-cache-timeout 60
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST dd if=/dev/zero of=$M0/file bs=1M count=20
TEST ln $M0/file $M0/linkey

EXPECT "20971520" stat -c %s $M0/linkey

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
