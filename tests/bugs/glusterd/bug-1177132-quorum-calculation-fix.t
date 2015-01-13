#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Lets create the volume and set quorum type as a server
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H2:$B2/${V0}1
TEST $CLI_1 volume set $V0 cluster.server-quorum-type server

# Start the volume
TEST $CLI_1 volume start $V0

# Set the quorum ratio to 80% which means in a two node cluster if one node is
# down quorum shouldn't be met and operations which goes through quorum
# validation should fail
TEST $CLI_1 volume set all cluster.server-quorum-ratio 80

# Bring down one glusterd instance
TEST kill_glusterd 2

# Now execute a command which goes through op state machine and it should fail
TEST ! $CLI_1 volume profile $V0 start

# Bring back the glusterd instance
TEST $glusterd_2

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

# Now re-execute the same profile command and this time it should succeed
TEST $CLI_1 volume profile $V0 start

cleanup;
