#!/bin/bash

# This test checks for if shd or any other daemons brought down (apart from
# brick processes) is not brought up automatically when glusterd on the other
# node is (re)started

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function shd_up_status_1 {
        $CLI_1 volume status | grep "localhost" | grep "Self-heal Daemon" | awk '{print $7}'
}

function shd_up_status_2 {
        $CLI_2 volume status | grep "localhost" | grep "Self-heal Daemon" | awk '{print $7}'
}

function get_shd_pid_2 {
        $CLI_2 volume status | grep "localhost" | grep "Self-heal Daemon" | awk '{print $8}'
}
cleanup;

TEST launch_cluster 3

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1  peer_count

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2  peer_count

# Lets create the volume
TEST $CLI_1 volume create $V0 replica 2 $H1:$B1/${V0}1 $H2:$B2/${V0}2

# Start the volume
TEST $CLI_1 volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" shd_up_status_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" shd_up_status_2

# Bring down shd on 2nd node
kill -15 $(get_shd_pid_2)

# Bring down glusterd on 1st node
TEST kill_glusterd 1

#Bring back 1st glusterd
TEST $glusterd_1

# We need to wait till PROCESS_UP_TIMEOUT and then check shd service does not
# come up on node 2
sleep $PROCESS_UP_TIMEOUT
EXPECT "N" shd_up_status_2

cleanup;
