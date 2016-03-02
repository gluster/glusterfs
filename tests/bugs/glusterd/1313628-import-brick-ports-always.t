#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

## Check that brick ports are always copied on import
## --------------------------------------------------
## This test checks that the brick ports are copied on import by checking that
## they don't change when the following happens,
##  - Stop a volume
##  - Stop glusterd
##  - Start the stopped volume
##  - Start the stopped glusterd

function get_brick_port() {
  local VOL=$1
  local BRICK=$2
  $CLI2 volume status $VOL $BRICK --xml | sed -ne 's/.*<port>\([0-9]*\)<\/port>/\1/p'
}


cleanup

TEST launch_cluster 2
TEST $CLI1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Create and start volume so that brick port assignment happens
TEST $CLI1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI1 volume start $V0

# Save port for 2nd brick
BPORT_ORIG=$(get_brick_port $V0 $H2:$B2/$V0)

# Stop volume, stop 2nd glusterd, start volume, start 2nd glusterd
TEST $CLI1 volume stop $V0
TEST kill_glusterd 2

TEST $CLI1 volume start $V0
TEST start_glusterd 2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Get new port and compare with old one
EXPECT_WITHIN $PROCESS_UP_TIMEOUT $BPORT_ORIG get_brick_port $V0 $H2:$B2/$V0

$CLI1 volume stop $V0

cleanup
