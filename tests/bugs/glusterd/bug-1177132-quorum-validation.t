#!/bin/bash

# Test case for quorum validation in glusterd for syncop framework

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc


cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Lets create the volume and set quorum type as a server
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H2:$B2/${V0}1
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server

# Start the volume
TEST $CLI_1 volume start $V0

# Set quorum ratio 52. means 52 % or more than 52% nodes of total available node
# should be available for performing volume operation.
# i.e. Server-side quorum is met if the number of nodes that are available is
# greater than or equal to 'quorum-ratio' times the number of nodes in the
# cluster

TEST $CLI_1 volume set all cluster.server-quorum-ratio 52

# Bring down 2nd glusterd
TEST kill_glusterd 2

# Now quorum is not meet. Add-brick, Remove-brick, volume-set command
#(Command based on syncop framework)should fail
TEST ! $CLI_1 volume add-brick $V0 $H1:$B1/${V0}1
TEST ! $CLI_1 volume remove-brick $V0 $H1:$B1/${V0}0 start
TEST ! $CLI_1 volume set $V0 barrier enable

# Now execute a command which goes through op state machine and it should fail
TEST ! $CLI_1 volume profile $V0 start

# Volume set all command and volume reset all command should be successful
TEST $CLI_1 volume set all cluster.server-quorum-ratio 80
TEST $CLI_1 volume reset all

# Bring back 2nd glusterd
TEST $glusterd_2

# After 2nd glusterd come back, there will be 2 nodes in a clusater
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

# Now quorum is meet.
# Add-brick, Remove-brick, volume-set command should success
TEST  $CLI_1 volume add-brick $V0 $H2:$B2/${V0}2
TEST  $CLI_1 volume remove-brick $V0 $H2:$B2/${V0}2 start
TEST  $CLI_1 volume set $V0 barrier enable
TEST  $CLI_1 volume remove-brick $V0 $H2:$B2/${V0}2 stop

## Stop the volume
TEST $CLI_1 volume stop $V0

## Bring down 2nd glusterd
TEST kill_glusterd 2

## Now quorum is not meet. Starting volume on 1st node should not success
TEST ! $CLI_1 volume start $V0

## Bring back 2nd glusterd
TEST $glusterd_2

# After 2nd glusterd come back, there will be 2 nodes in a clusater
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

## Now quorum is meet. Starting volume on 1st node should be success.
TEST $CLI_1 volume start $V0

# Now re-execute the same profile command and this time it should succeed
TEST $CLI_1 volume profile $V0 start

cleanup;
