#!/bin/bash

## Test case for stopping all running daemons service on peer detach.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;


## Start a 2 node virtual cluster
TEST launch_cluster 2;

## Peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count


## Creating and starting volume
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H1:$B1/${V0}1
TEST $CLI_1 volume start $V0

## To Do: Add test case for quota and snapshot daemon. Currently quota
##        Daemon is not working in cluster framework. And sanpd daemon
##        Start only in one node in cluster framework. Add test case
##        once patch http://review.gluster.org/#/c/11666/ merged,

## We are having 2 node "nfs" daemon should run on both node.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_nfs_count

## Detach 2nd node from the cluster.
TEST $CLI_1 peer detach $H2;


## After detaching 2nd node we will have only 1 nfs and quota daemon running.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_nfs_count

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
