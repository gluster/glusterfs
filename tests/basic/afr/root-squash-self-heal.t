#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 server.root-squash on
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --no-root-squash=yes --use-readdirp=no $M0
TEST kill_brick $V0 $H0 $B0/${V0}0
echo abc > $M0/a

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
find $M0 | xargs stat > /dev/null
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

cleanup
