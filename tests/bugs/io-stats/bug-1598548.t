#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

checkdumpthread () {
        local brick_pid=$(get_brick_pid $1 $2 $3)
        local thread_count=$(gstack $brick_pid | grep -c _ios_dump_thread)
        echo $thread_count
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0

TEST $CLI volume profile $V0 start
EXPECT 0 checkdumpthread $V0 $H0 $B0/${V0}0

TEST $CLI volume set $V0 diagnostics.stats-dump-interval 3
EXPECT 1 checkdumpthread $V0 $H0 $B0/${V0}0

TEST $CLI volume set $V0 diagnostics.stats-dump-interval 10
EXPECT 1 checkdumpthread $V0 $H0 $B0/${V0}0

TEST $CLI volume set $V0 diagnostics.stats-dump-interval 0
EXPECT 0 checkdumpthread $V0 $H0 $B0/${V0}0

TEST $CLI volume set $V0 diagnostics.stats-dump-interval 7
EXPECT 1 checkdumpthread $V0 $H0 $B0/${V0}0

TEST $CLI volume set $V0 diagnostics.stats-dump-interval 0
EXPECT 0 checkdumpthread $V0 $H0 $B0/${V0}0

TEST $CLI volume set $V0 diagnostics.stats-dump-interval 11
EXPECT 1 checkdumpthread $V0 $H0 $B0/${V0}0

cleanup;
