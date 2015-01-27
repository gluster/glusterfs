#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version
TEST launch_cluster 3
TEST setup_lvm 3

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 peer probe $H3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2 $H3:$L3
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'


kill_glusterd 3
# If glusterd-quorum is not met then snapshot-create should fail
TEST ! $CLI_1 snapshot create ${V0}_snap1 ${V0} no-timestamp

cleanup;
