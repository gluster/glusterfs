#!/bin/bash

# Test that a volume becomes unwritable when the cluster loses quorum.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../cluster.rc


function check_fs {
	df $1 &> /dev/null
	echo $?
}

function check_peers {
	$CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

function glusterfsd_count {
    pidof glusterfsd | wc -w;
}

cleanup;

TEST launch_cluster 3; # start 3-node virtual cluster
TEST $CLI_1 peer probe $H2; # peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H3; # peer probe server 3 from server 1 cli

EXPECT_WITHIN 20 2 check_peers

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server
TEST $CLI_1 volume start $V0
TEST glusterfs --volfile-server=$H1 --volfile-id=$V0 $M0

# Kill one pseudo-node, make sure the others survive and volume stays up.
TEST kill_node 3;
EXPECT_WITHIN 20 1 check_peers;
EXPECT 0 check_fs $M0;
EXPECT 2 glusterfsd_count;

# Kill another pseudo-node, make sure the last one dies and volume goes down.
TEST kill_node 2;
EXPECT_WITHIN 20 0 check_peers
EXPECT 1 check_fs $M0;
EXPECT 0 glusterfsd_count; # the two glusterfsds of the other two glusterds
                           # must be dead

TEST $glusterd_2;
TEST $glusterd_3;
EXPECT_WITHIN 20 3 glusterfsd_count; # restore quorum, all ok
EXPECT_WITHIN 5 0 check_fs $M0;

cleanup
