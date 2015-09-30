#!/bin/bash

#Test case: Create a distributed replicate volume, and reduce
#replica count

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a 2X3 distributed-replicate volume
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..6};
TEST $CLI volume start $V0

# Reduce to 2x2 volume by specifying bricks in reverse order
function remove_brick_status {
        $CLI volume remove-brick $V0 replica 2 \
        $H0:$B0/${V0}6  $H0:$B0/${V0}3 force 2>&1 |grep -oE "success|failed"
}
EXPECT "success"  remove_brick_status;

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
