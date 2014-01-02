#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

# Create a 2x2 dist-rep volume; peer probe a new node.
# Performing remove-brick from this new node must succeed
# without crashing it's glusterd

TEST launch_cluster 2;
TEST $CLI_1 volume create $V0 replica 2 $H1:$B1/${V0}{1,2,3,4}
TEST $CLI_1 volume start $V0;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN 20 1 check_peers;
TEST $CLI_2 volume remove-brick $V0 $H1:$B1/${V0}{3,4} start;
TEST $CLI_2 volume info
cleanup;
