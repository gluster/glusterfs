#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
cleanup;

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

TEST launch_cluster 3
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

TEST $CLI_1 volume create $V0 replica 2 $H1:$B0/${V0} $H2:$B0/${V0}
TEST $CLI_1 volume start $V0

# Negative case with brick not killed && volume-id xattrs present
TEST ! $CLI_1 volume reset-brick $V0 $H1:$B0/${V0} $H1:$B0/${V0} commit force

TEST $CLI_1 volume reset-brick $V0 $H1:$B0/${V0} start
# Now test if reset-brick commit force works
TEST $CLI_1 volume reset-brick $V0 $H1:$B0/${V0} $H1:$B0/${V0} commit force

cleanup;
