#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H2:$B2/${V0}1
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server
TEST $CLI_1 volume start $V0

#bug-1177132 - sync server quorum options when a node is brought up
TEST $CLI_1 volume set all cluster.server-quorum-ratio 52

#Bring down 2nd glusterd
TEST kill_glusterd 2

#bug-1104642 - sync server quorum options when a node is brought up
#set the volume all options from the 1st glusterd
TEST $CLI_1 volume set all cluster.server-quorum-ratio 80

# Now quorum is not meet. Add-brick, Remove-brick, volume-set command
#(Command based on syncop framework)should fail
TEST ! $CLI_1 volume add-brick $V0 $H1:$B1/${V0}2
TEST ! $CLI_1 volume remove-brick $V0 $H1:$B1/${V0}0 start
TEST ! $CLI_1 volume set $V0 barrier enable

# Now execute a command which goes through op state machine and it should fail
TEST ! $CLI_1 volume profile $V0 start

#Bring back the 2nd glusterd
TEST $glusterd_2

#verify whether the value has been synced
EXPECT_WITHIN $PROBE_TIMEOUT "80" volinfo_field_1 all cluster.server-quorum-ratio
EXPECT_WITHIN $PROBE_TIMEOUT '1' peer_count
EXPECT_WITHIN $PROBE_TIMEOUT "80" volinfo_field_2 all cluster.server-quorum-ratio

# Now quorum is meet.
# Add-brick, Remove-brick, volume-set command should success
TEST  $CLI_1 volume add-brick $V0 $H2:$B2/${V0}2
TEST  $CLI_1 volume remove-brick $V0 $H2:$B2/${V0}2 start
TEST  $CLI_1 volume set $V0 barrier enable
TEST  $CLI_1 volume remove-brick $V0 $H2:$B2/${V0}2 stop

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}1

## Stop the volume
TEST $CLI_1 volume stop $V0

## Bring down 2nd glusterd
TEST kill_glusterd 2

## Now quorum is not meet. Starting volume on 1st node should not success
TEST ! $CLI_1 volume start $V0

## Bring back 2nd glusterd
TEST $glusterd_2

# After 2nd glusterd come back, there will be 2 nodes in a cluster
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

## Now quorum is meet. Starting volume on 1st node should be success.
TEST $CLI_1 volume start $V0

# Now re-execute the same profile command and this time it should succeed
TEST $CLI_1 volume profile $V0 start

#bug-1352277

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}1

TEST $CLI_1 volume set $V0 cluster.server-quorum-type none

# Bring down all the gluster processes
TEST killall_gluster

#bring back 1st glusterd and check whether the brick process comes back
TEST $glusterd_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}0

#enabling quorum should bring down the brick
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status_1 $V0 $H1 $B1/${V0}0

TEST $glusterd_2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}1

#bug-1367478 - brick processes should not be up when quorum is not met
TEST $CLI_1 volume create $V1 $H1:$B1/${V1}1 $H2:$B2/${V1}2
TEST $CLI_1 volume start $V1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H1 $B1/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H2 $B2/${V1}2

# Restart 2nd glusterd
TEST kill_glusterd 2
TEST $glusterd_2

# Check if all bricks are up
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H1 $B1/${V1}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V1 $H2 $B2/${V1}2

cleanup
