#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST verify_lvm_version
TEST launch_cluster 2
TEST setup_lvm 1

TEST $CLI_1 volume create $V0 $H1:$L1
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

# Quota is not working with cluster test framework
# Need to check why, Until then commenting out this
#TEST $CLI_1 volume quota $V0 enable
#EXPECT 'on' volinfo_field $V0 'features.quota'

TEST $CLI_1 snapshot create ${V0}_snap $V0
EXPECT '1' get_snap_count CLI_1 $V0

TEST $CLI_1 volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status'
EXPECT "1" get_aux

TEST $CLI_1 snapshot restore $($CLI_1 snapshot list)
EXPECT '0' get_snap_count CLI_1 $V0

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

cleanup;
