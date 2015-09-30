#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

cleanup

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

kill_glusterd 2
TEST start_glusterd 2

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

#volume stop should not crash
TEST $CLI_2 volume stop $V0

# check whether glusterd instance is running on H2 as this is the node which
# restored the volume configuration after a restart
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count
cleanup
