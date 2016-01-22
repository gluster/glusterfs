#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
    $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}
cleanup;

#setup cluster and test volume
TEST launch_cluster 3; # start 3-node virtual cluster
TEST $CLI_1 peer probe $H2; # peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H3; # peer probe server 3 from server 1 cli

EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers;

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0
TEST $CLI_1 volume start $V0

# there is no gfapi application to take statedumps yet, it will get added in
# the next patch, this only tests the CLI for correctness

cleanup_statedump

TEST ! $CLI_1 volume statedump $V0 client $H2:0
TEST ! $CLI_2 volume statedump $V0 client $H2:-1
TEST $CLI_3 volume statedump $V0 client $H2:765
TEST ! $CLI_1 volume statedump $V0 client $H2:
TEST ! $CLI_2 volume statedump $V0 client

cleanup_statedump
cleanup;
