#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

# Check if xml output is generated correctly for volume status for a single brick
# present on another peer and no async tasks are running.

function get_peer_count {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}
cleanup

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 get_peer_count
TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

TEST $CLI_1 volume status $V0 $H2:$B2/$V0 --xml

TEST $CLI_1 volume stop $V0

cleanup
