#!/bin/bash

# Test that a volume becomes unwritable when the cluster loses quorum.

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

TEST launch_cluster 3; # start 3-node virtual cluster
TEST $CLI_1 peer probe $H2; # peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H3; # peer probe server 3 from server 1 cli

EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server
TEST $CLI_1 volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 online_brick_count;

TEST glusterfs --volfile-server=$H1 --volfile-id=$V0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 check_fs $M0;

# Kill one pseudo-node, make sure the others survive and volume stays up.
TEST kill_node 3;
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 online_brick_count;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 check_fs $M0;

# Kill another pseudo-node, make sure the last one dies and volume goes down.
TEST kill_node 2;
EXPECT_WITHIN $PROBE_TIMEOUT 0 check_peers
#two glusterfsds of the other two glusterds must be dead
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 online_brick_count;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 check_fs $M0;

TEST $glusterd_2;
TEST $glusterd_3;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 online_brick_count; # restore quorum, all ok
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 check_fs $M0;

cleanup
