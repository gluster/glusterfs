#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

TEST $CLI_1 volume create $V0 $H1:$B1/$V0
TEST $CLI_1 volume create $V1 $H1:$B1/$V1
TEST $CLI_1 volume start $V0
TEST $CLI_1 volume start $V1

for i in {1..20}
do
    $CLI_1 volume set $V0 diagnostics.client-log-level DEBUG &
    $CLI_1 volume set $V1 barrier on
    $CLI_2 volume set $V0 diagnostics.client-log-level DEBUG &
    $CLI_2 volume set $V1 barrier on
done

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers
TEST $CLI_1 volume status
TEST $CLI_2 volume status

cleanup;
