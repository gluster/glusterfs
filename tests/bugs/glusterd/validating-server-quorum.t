#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function check_fs {
        df $1 &> /dev/null
        echo $?
}

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

TEST launch_cluster 3

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1  peer_count

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2  peer_count

# Lets create the volume
TEST $CLI_1 volume create $V0 replica 3 $H1:$B1/${V0}1 $H2:$B2/${V0}2 $H3:$B3/${V0}3
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server

# Start the volume
TEST $CLI_1 volume start $V0

#bug-1345727 - bricks should be down when quorum is not met

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H3 $B3/${V0}3

# Bring down glusterd on 2nd node
TEST kill_glusterd 2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST kill_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 0 peer_count

# Server quorum is not met. Brick on 1st node must be down
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "0" brick_up_status_1 $V0 $H1 $B1/${V0}1

# Set quorum ratio 95. means 95 % or more than 95% nodes of total available node
# should be available for performing volume operation.
# i.e. Server-side quorum is met if the number of nodes that are available is
# greater than or equal to 'quorum-ratio' times the number of nodes in the
# cluster
TEST $CLI_1 volume set all cluster.server-quorum-ratio 95

#bug-1483058 - replace-brick should fail when quorum is not met
TEST ! $CLI_1 volume replace-brick $V0 $H2:$B2/${V0}2 $H1:$B1/${V0}2_new commit force

#Bring back 2nd glusterd
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

# quorum is met. replace-brick will execute successfully
EXPECT_WITHIN $PEER_SYNC_TIMEOUT 0 attempt_replace_brick 1 $V0 $H2:$B2/${V0}2 $H2:$B2/${V0}2_new

TEST $CLI_1 volume reset all
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2_new
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H3 $B3/${V0}3


#bug-913555 - volume should become unwritable when quorum does not met

TEST glusterfs --volfile-server=$H1 --volfile-id=$V0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 check_fs $M0;

# Kill one pseudo-node, make sure the others survive and volume stays up.
TEST kill_node 3;
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}2_new
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 check_fs $M0;

# Kill another pseudo-node, make sure the last one dies and volume goes down.
TEST kill_node 2;
EXPECT_WITHIN $PROBE_TIMEOUT 0 check_peers
#two glusterfsds of the other two glusterds must be dead
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status_1 $V0 $H1 $B1/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 check_fs $M0;

TEST $glusterd_2;
TEST $glusterd_3;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 check_fs $M0;

cleanup
