#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

#This tests if the arbiter-count is transferred to the other peer.
function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

kill_glusterd 2
$CLI_1 volume create $V0 replica 3 arbiter 1 $H0:$B0/b{1..3}
TEST $glusterd_2
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field_1 $V0 "Number of Bricks"
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field_2 $V0 "Number of Bricks"

cleanup;
