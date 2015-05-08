#!/bin/bash

## Test case for bitrot
## bitd daemon should not start on the node which dont have any brick


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

function get_bitd_count {
        ps auxw | grep glusterfs | grep bitd.pid | grep -v grep | wc -l
}

## Start a 2 node virtual cluster
TEST launch_cluster 2;

## Peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

## Creating a volume which is having brick only on one node
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H1:$B1/${V0}1

## Start the volume
TEST $CLI_1 volume start $V0

## Enable bitrot on volume from 2nd node.
TEST $CLI_2 volume bitrot $V0 enable

## Bitd daemon should be running on the node which is having brick. Here node1
## only have brick so bitrot daemon count value should be 1.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

## Bitd daemon should not run on 2nd node and it should not create bitrot
## volfile on this node. Below test case it to check whether its creating bitrot
## volfile or not for 2nd node which dont have any brick.
## Get current working directory of 2nd node which dont have any brick and do
## stat on bitrot volfile.

cur_wrk_dir2=$($CLI_2 system:: getwd)
TEST ! stat $cur_wrk_dir2/bitd/bitd-server.vol


## Bitd daemon should run on 1st node and it should create bitrot
## volfile on this node. Below test case it to check whether its creating bitrot
## volfile or not for 1st node which is having brick.
## Get current working directory of 1st node which have brick and do
## stat on bitrot volfile.

cur_wrk_dir1=$($CLI_1 system:: getwd)
TEST stat $cur_wrk_dir1/bitd/bitd-server.vol

cleanup;
