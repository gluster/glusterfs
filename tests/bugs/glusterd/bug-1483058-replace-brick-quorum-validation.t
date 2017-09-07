#!/bin/bash

# Test case for quorum validation in glusterd for syncop framework

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc


cleanup;

TEST launch_cluster 3

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

# Lets create the volume and set quorum type as a server
TEST $CLI_1 volume create $V0 replica 3 $H1:$B1/${V0}0 $H2:$B2/${V0}1 $H3:$B3/${V0}2
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server

# Start the volume
TEST $CLI_1 volume start $V0

# Set quorum ratio 95. means 95 % or more than 95% nodes of total available node
# should be available for performing volume operation.
# i.e. Server-side quorum is met if the number of nodes that are available is
# greater than or equal to 'quorum-ratio' times the number of nodes in the
# cluster

TEST $CLI_1 volume set all cluster.server-quorum-ratio 95
# Bring down 2nd glusterd
TEST kill_glusterd 2

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Now quorum is not meet. Now execute replace-brick command
# This command should fail as cluster is not in quorum
TEST ! $CLI_1 volume replace-brick $V0 $H2:$B2/${V0}1 $H1:$B1/${V0}1_new commit force

# Bring 2nd glusterd up
TEST start_glusterd 2

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

# Now quorum is met. replace-brick will execute successfuly
TEST  $CLI_1 volume replace-brick $V0 $H2:$B2/${V0}1 $H1:$B1/${V0}1_new commit force

#cleanup;
