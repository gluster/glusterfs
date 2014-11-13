#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../cluster.rc

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

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI_1 volume info $vol | grep "^$field: " | sed 's/.*: //';
}

function two_diff_vols_create {
        # Both volume creates should be successful
        $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0 &
        PID_1=$!

        $CLI_2 volume create $V1 $H1:$B1/$V1 $H2:$B2/$V1 $H3:$B3/$V1 &
        PID_2=$!

        wait $PID_1 $PID_2
}

function two_diff_vols_start {
        # Both volume starts should be successful
        $CLI_1 volume start $V0 &
        PID_1=$!

        $CLI_2 volume start $V1 &
        PID_2=$!

        wait $PID_1 $PID_2
}

function two_diff_vols_stop_force {
        # Force stop, so that if rebalance from the
        # remove bricks is in progress, stop can
        # still go ahead. Both volume stops should
        # be successful
        $CLI_1 volume stop $V0 force &
        PID_1=$!

        $CLI_2 volume stop $V1 force &
        PID_2=$!

        wait $PID_1 $PID_2
}

function same_vol_remove_brick {

        # Running two same vol commands at the same time can result in
        # two success', two failures, or one success and one failure, all
        # of which are valid. The only thing that shouldn't happen is a
        # glusterd crash.

        local vol=$1
        local brick=$2
        $CLI_1 volume remove-brick $1 $2 start &
        PID=$!
        $CLI_2 volume remove-brick $1 $2 start
        wait $PID
}

cleanup;

TEST launch_cluster 3;
TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;

EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers

two_diff_vols_create
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Created' volinfo_field $V1 'Status';

two_diff_vols_start
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT 'Started' volinfo_field $V1 'Status';

same_vol_remove_brick $V0 $H2:$B2/$V0
# Checking glusterd crashed or not after same volume remove brick
# on both nodes.
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers

same_vol_remove_brick $V1 $H2:$B2/$V1
# Checking glusterd crashed or not after same volume remove brick
# on both nodes.
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers

$CLI_1 volume set $V0 diagnostics.client-log-level DEBUG &
PID=$!
$CLI_1 volume set $V1 diagnostics.client-log-level DEBUG
wait $PID
kill_glusterd 3
$CLI_1 volume status $V0
$CLI_2 volume status $V1
$CLI_1 peer status
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT 'Started' volinfo_field $V1 'Status';

TEST $glusterd_3
$CLI_1 volume status $V0
$CLI_2 volume status $V1
$CLI_1 peer status
cleanup;
