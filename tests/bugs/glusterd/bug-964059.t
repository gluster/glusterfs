#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

function volume_count {
        local cli=$1;
        if [ $cli -eq '1' ] ; then
                $CLI_1 volume info | grep 'Volume Name' | wc -l;
        else
                $CLI_2 volume info | grep 'Volume Name' | wc -l;
        fi
}

cleanup;

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0
TEST $CLI_1 volume remove-brick $V0 $H2:$B2/$V0 start
TEST $CLI_1 volume status
cleanup;
