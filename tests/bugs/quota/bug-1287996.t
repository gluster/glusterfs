#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

TEST launch_cluster 2;

TEST $CLI_1 volume create $V0 $H1:$B1/$V0
TEST $CLI_1 volume start $V0
TEST $CLI_1 volume quota $V0 enable

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

cleanup;
