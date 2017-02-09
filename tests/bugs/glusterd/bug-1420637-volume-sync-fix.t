#!/bin/bash

# Test case for checking when server-quorum-ratio value is changed on one
# glusterd where the other is down, the other changes done get synced back
properly when the glusterd is brought up.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

# Lets create & start the volume
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H2:$B2/${V0}1

# Start the volume
TEST $CLI_1 volume start $V0
TEST $CLI_1 volume set $V0 performance.readdir-ahead on

# Bring down 2nd glusterd
TEST kill_glusterd 2

TEST $CLI_1 volume set all cluster.server-quorum-ratio 60
TEST $CLI_1 volume set $V0 performance.readdir-ahead off

# Bring back 2nd glusterd
TEST $glusterd_2

# After 2nd glusterd come back, there will be 2 nodes in a clusater
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

EXPECT_WITHIN $PROBE_TIMEOUT "60" volinfo_field_2 all cluster.server-quorum-ratio
EXPECT_WITHIN $PROBE_TIMEOUT "off" volinfo_field_2 $V0 performance.readdir-ahead

cleanup;
