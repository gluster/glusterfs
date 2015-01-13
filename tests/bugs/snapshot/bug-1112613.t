#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../cluster.rc

cleanup;

V1="patchy2"

TEST verify_lvm_version;
TEST launch_cluster 2
TEST setup_lvm 2

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1
TEST $CLI_1 volume start $V0
TEST $CLI_1 volume create $V1 $H2:$L2
TEST $CLI_1 volume start $V1

# Create 3 snapshots for volume $V0
snap_count=3
snap_index=1
TEST snap_create CLI_1 $V0 $snap_index $snap_count;

# Create 3 snapshots for volume $V1
snap_count=4
snap_index=11
TEST snap_create CLI_1 $V1 $snap_index $snap_count;

EXPECT '3' get_snap_count CLI_1 $V0;
EXPECT '4' get_snap_count CLI_1 $V1;
EXPECT '7' get_snap_count CLI_1

TEST $CLI_1 snapshot delete volume $V0
EXPECT '0' get_snap_count CLI_1 $V0;
EXPECT '4' get_snap_count CLI_1 $V1;
EXPECT '4' get_snap_count CLI_1

TEST $CLI_1 snapshot delete all
EXPECT '0' get_snap_count CLI_1 $V0;
EXPECT '0' get_snap_count CLI_1 $V1;
EXPECT '0' get_snap_count CLI_1

cleanup;

