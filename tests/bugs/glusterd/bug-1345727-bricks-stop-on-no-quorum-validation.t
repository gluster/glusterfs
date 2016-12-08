#!/bin/bash

# Test case to check if bricks are down when quorum is not met

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 3

TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

# Lets create the volume and set quorum type as a server
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}1 $H2:$B2/${V0}2 $H3:$B3/${V0}3
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server

# Start the volume
TEST $CLI_1 volume start $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H3 $B3/${V0}3

# Bring down 2nd and 3rd glusterd
TEST kill_glusterd 2
TEST kill_glusterd 3

# Server quorum is not met. Brick on 1st node must be down
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" brick_up_status_1 $V0 $H1 $B1/${V0}1

# Set quorum ratio 95. means 95 % or more than 95% nodes of total available node
# should be available for performing volume operation.
# i.e. Server-side quorum is met if the number of nodes that are available is
# greater than or equal to 'quorum-ratio' times the number of nodes in the
# cluster

TEST $CLI_1 volume set all cluster.server-quorum-ratio 95

# Bring back 2nd glusterd
TEST $glusterd_2

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Server quorum is still not met. Bricks should be down on 1st and 2nd nodes
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" brick_up_status_1 $V0 $H2 $B2/${V0}2

# Bring back 3rd glusterd
TEST $glusterd_3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

# Server quorum is met now. Bricks should be up on all nodes
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H3 $B3/${V0}3

cleanup;

