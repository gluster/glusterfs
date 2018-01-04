#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function peer_count {
eval \$CLI_$1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

## start a 3 node virtual cluster
TEST launch_cluster 3;

## peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 1

#test case for bug 1266818 - disabling enable-shared-storage option
##should not delete user created volume with name glusterd_shared_storage

## creating a volume with name glusterd_shared_storage
TEST $CLI_1 volume create glusterd_shared_storage  $H1:$B1/${V0}0 $H2:$B2/${V0}1
TEST $CLI_1 volume start glusterd_shared_storage

## disabling enable-shared-storage should not succeed and should not delete the
## user created volume with name "glusterd_shared_storage"
TEST ! $CLI_1 volume all enable-shared-storage disable

## volume with name should exist
TEST $CLI_1 volume info glusterd_shared_storage

#testcase: bug-1245045-remove-brick-validation

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

kill_glusterd 2

#remove-brick should fail as the peer hosting the brick is down
TEST ! $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} start

TEST $glusterd_2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1

#volume status should work
TEST $CLI_2 volume status

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 3
TEST $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} start
kill_glusterd 2

#remove-brick commit should fail as the peer hosting the brick is down
TEST ! $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} commit

TEST $glusterd_2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1

#volume status should work
TEST $CLI_2 volume status

TEST $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} stop

kill_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 1

TEST $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} start

TEST start_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1
TEST $CLI_3 volume status

cleanup
