#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2;

# Fool the cluster to operate with 3.5 version even though binary's op-version
# is > 3.5. This is to ensure 3.5 code path is hit to test that volume status
# works when a node is upgraded from 3.5 to 3.7 or higher as mgmt_v3 lock is
# been introduced in 3.6 version and onwards

GD1_WD=$($CLI_1 system getwd)
$CLI_1 system uuid get
TEST sed -rnie "'s/(operating-version=)\w+/\130500/gip'" ${GD1_WD}/glusterd.info

TEST kill_glusterd 1
TEST start_glusterd 1

TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

TEST $CLI_1 volume status $V0;

cleanup;
