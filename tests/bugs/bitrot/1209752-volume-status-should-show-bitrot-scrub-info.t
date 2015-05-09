#!/bin/bash

## Test case for bitrot
## gluster volume status command should show status of bitrot daemon


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;


## Start a 2 node virtual cluster
TEST launch_cluster 2;

## Peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

## Lets create and start the volume
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H2:$B2/${V0}1
TEST $CLI_1 volume start $V0

## Enable bitrot on volume $V0
TEST $CLI_1 volume bitrot $V0 enable

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_bitd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_scrubd_count

## From node 1 Gluster volume status command should show the status of bitrot
## daemon of all the nodes. there are 2 nodes in a cluster with having brick
## ${V0}1 and ${V0}2 . So there should be 2 bitrot daemon running.

bitd=$($CLI_1 volume status $V0 | grep "Bitrot Daemon" | grep -v grep | wc -l)
TEST [ "$bitd" -eq 2 ];



## From node 2 Gluster volume status command should show the status of Scrubber
## daemon of all the nodes. There are 2 nodes in a cluster with having brick
## ${V0}1 and ${V0}2 . So there should be 2 Scrubber daemon running.

scrub=$($CLI_2 volume status $V0 | grep "Scrubber Daemon" | grep -v grep | \
        wc -l)
TEST [ "$scrub" -eq 2 ];



## From node 1 Gluster volume status command should print status of only
## scrubber daemon. There should be total 2 scrubber daemon running, one daemon
## for each node

scrub=$($CLI_1 volume status $V0 scrub | grep "Scrubber Daemon" | \
        grep -v grep | wc -l)
TEST [ "$scrub" -eq 2 ];



## From node 2 Gluster volume status command should print status of only
## bitd daemon. There should be total 2 bitd daemon running, one daemon
## for each node

bitd=$($CLI_2 volume status $V0 bitd | grep "Bitrot Daemon" | \
       grep -v grep | wc -l)
TEST [ "$bitd" -eq 2 ];


cleanup;
