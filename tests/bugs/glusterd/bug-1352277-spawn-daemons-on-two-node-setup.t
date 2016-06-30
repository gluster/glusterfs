#!/bin/bash

# Test case for checking whether the brick process(es) come up on a two node
# cluster if one of them is already down and other is going through a restart

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1  peer_count

# Lets create the volume
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}1 $H2:$B2/${V0}2

# Start the volume
TEST $CLI_1 volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2

# Bring down all the gluster processes
TEST killall_gluster

#Bring back 1st glusterd and check whether the brick process comes back
TEST $glusterd_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1

#Enabling quorum should bring down the brick
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status_1 $V0 $H1 $B1/${V0}1

cleanup;
