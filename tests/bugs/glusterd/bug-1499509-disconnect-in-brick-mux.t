#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup

TEST glusterd
TEST pidof glusterd

## Enable brick multiplexing
TEST $CLI volume set all cluster.brick-multiplex on

## creating 1x3 replicated volumes
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}_{1..3}
TEST $CLI volume create $V1 replica 3 $H0:$B1/${V1}_{1..3}

## Start the volume
TEST $CLI volume start $V0
TEST $CLI volume start $V1

kill -9 $(pgrep glusterfsd)

EXPECT 0 online_brick_count

cleanup
