#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

function count_brick_pids {
        $CLI_1 --xml volume status all | sed -n '/.*<pid>\([^<]*\).*/s//\1/p' \
                                     | grep -v "N/A" | sort | uniq | wc -l
}

function count_N/A_brick_pids {
        $CLI_1 --xml volume status all | sed -n '/.*<pid>\([^<]*\).*/s//\1/p' \
                                     | grep -- '\-1' | sort | uniq | wc -l
}

function check_peers {
        $CLI_2 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

TEST launch_cluster 3
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 volume set all cluster.brick-multiplex on
#bug-1609163 - bricks of normal volume should not attach to bricks of gluster_shared_storage volume

##Create, start and mount meta_volume i.e., shared_storage
TEST $CLI_1 volume create $META_VOL replica 3 $H1:$B1/${META_VOL}1 $H2:$B2/${META_VOL}1 $H3:$B3/${META_VOL}1
TEST $CLI_1 volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H1 --volfile-id $META_VOL $META_MNT

TEST $CLI_1 volume info gluster_shared_storage

EXPECT 3 count_brick_processes

#create and start a new volume
TEST $CLI_1 volume create $V0 replica 3 $H1:$B1/${V0}{1..3} $H2:$B2/${V0}{1..3}
TEST $CLI_1 volume start $V0

# bricks of normal volume should not attach to bricks of gluster_shared_storage volume
EXPECT 5 count_brick_processes

#bug-1549996 - stale brick processes on the nodes after volume deletion

TEST $CLI_1 volume create $V1 replica 3 $H1:$B1/${V1}{1..3} $H2:$B2/${V1}{1..3}
TEST $CLI_1 volume start $V1

EXPECT 5 count_brick_processes

TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume stop $V1

EXPECT 3 count_brick_processes

TEST $CLI_1 volume stop $META_VOL

TEST $CLI_1 volume delete $META_VOL
TEST $CLI_1 volume delete $V0
TEST $CLI_1 volume delete $V1

#bug-1773856 - Brick process fails to come up with brickmux on

TEST $CLI_1 volume create $V0 $H1:$B1/${V0}1 $H2:$B2/${V0}1 $H3:$B3/${V0}1 force
TEST $CLI_1 volume start $V0


EXPECT 3 count_brick_processes

#create and start a new volume
TEST $CLI_1 volume create $V1 $H1:$B1/${V1}2 $H2:$B2/${V1}2 $H3:$B3/${V1}2 force
TEST $CLI_1 volume start $V1

EXPECT 3 count_brick_processes

V2=patchy2
TEST $CLI_1 volume create $V2 $H1:$B1/${V2}3 $H2:$B2/${V2}3 $H3:$B3/${V2}3 force
TEST $CLI_1 volume start $V2

EXPECT 3 count_brick_processes

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_brick_pids

TEST kill_node 1

sleep 10

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers;

$CLI_2 volume set $V0 performance.readdir-ahead on
$CLI_2 volume set $V1 performance.readdir-ahead on

TEST $glusterd_1;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 count_brick_pids
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0  count_N/A_brick_pids

cleanup;
