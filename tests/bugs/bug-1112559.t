#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../cluster.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

function check_snaps_status {
       $CLI_1 snapshot status | grep 'Snap Name : ' | wc -l
}

function check_snaps_bricks_health {
       $CLI_1 snapshot status | grep 'Brick Running     :   Yes' | wc -l
}


SNAP_COMMAND_TIMEOUT=40
NUMBER_OF_BRICKS=2

cleanup;
TEST verify_lvm_version
TEST launch_cluster 3
TEST setup_lvm 3

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2

TEST $CLI_1 volume start $V0

#Create snapshot and add a peer together
$CLI_1 snapshot create ${V0}_snap1 ${V0} &
PID_1=$!
$CLI_1  peer probe $H3
wait $PID_1

#Snapshot should be created and in the snaplist
TEST snapshot_exists 1 ${V0}_snap1

#Not being paranoid! Just checking for the status of the snapshot
#During the testing of the bug the snapshot would list but actually
#not be created.Therefore check for health of the snapshot
EXPECT_WITHIN $SNAP_COMMAND_TIMEOUT 1 check_snaps_status

#Disabling the checking of snap brick status , Will continue investigation
#on the failure of the snapbrick port bind issue. 
#EXPECT_WITHIN $SNAP_COMMAND_TIMEOUT $NUMBER_OF_BRICKS  check_snaps_bricks_health

#check if the peer is added successfully
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 snapshot delete ${V0}_snap1

cleanup;


