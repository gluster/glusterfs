#!/bin/bash

# Test case to check for successful startup of volume bricks on glusterd restart

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Lets create the volume and set quorum type as a server
TEST $CLI_1 volume create $V0 replica 2 $H1:$B1/${V0}1 $H2:$B2/${V0}2
TEST $CLI_1 volume create $V1 replica 2 $H1:$B1/${V1}1 $H2:$B2/${V1}2

# Start the volume
TEST $CLI_1 volume start $V0
TEST $CLI_1 volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H1 $B1/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H2 $B2/${V1}2

# Restart 2nd glusterd
TEST kill_glusterd 2
TEST $glusterd_2

# Check if all bricks are up
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H1 $B1/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H2 $B2/${V1}2

cleanup;

