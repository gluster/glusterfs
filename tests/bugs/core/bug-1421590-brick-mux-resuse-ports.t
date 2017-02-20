#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../volume.rc

function get_nth_brick_port_for_volume () {
        local VOL=$1
        local n=$2

        $CLI volume status $VOL --xml | sed -ne 's/.*<port>\([-0-9]*\)<\/port>/\1/p' \
                                      | head -n $n | tail -n 1
}

TEST glusterd

TEST $CLI volume set all cluster.brick-multiplex on
push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

TEST $CLI volume create $V0 $H0:$B0/brick{0,1}
TEST $CLI volume start $V0

port_brick0=$(get_nth_brick_port_for_volume $V0 1)

# restart the volume
TEST $CLI volume stop $V0
TEST $CLI volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT $port_brick0 get_nth_brick_port_for_volume $V0 1

TEST $CLI volume stop $V0
TEST $CLI volume set all cluster.brick-multiplex off

TEST $CLI volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT $port_brick0 get_nth_brick_port_for_volume $V0 1

port_brick1=$(get_nth_brick_port_for_volume $V0 2)

# restart the volume
TEST $CLI volume stop $V0
TEST $CLI volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT $port_brick0 get_nth_brick_port_for_volume $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT $port_brick1 get_nth_brick_port_for_volume $V0 2

TEST $CLI volume stop $V0

TEST $CLI volume set all cluster.brick-multiplex on

TEST $CLI volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT $port_brick0 get_nth_brick_port_for_volume $V0 1

