#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 4;

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count
TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0


TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 peer probe $H4;
EXPECT_WITHIN $PROBE_TIMEOUT 3 peer_count

# peers hosting bricks can't be detached
TEST ! $CLI_3 peer detach $H1
TEST ! $CLI_3 peer detach $H2


# peer not hosting bricks should be detachable
TEST $CLI_3 peer detach $H4
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count
cleanup;
