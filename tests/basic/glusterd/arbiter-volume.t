#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This command tests the volume create command validation for arbiter volumes.

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/b{4..9}
EXPECT "2 x \(2 \+ 1\) = 6" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 stripe 2 replica 3 arbiter 1 $H0:$B0/b{10..15}
EXPECT "1 x 2 x \(2 \+ 1\) = 6" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST rm -rf $B0/b{1..3}
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"
TEST killall -15 glusterd
TEST glusterd
TEST pidof glusterd
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

#cleanup
