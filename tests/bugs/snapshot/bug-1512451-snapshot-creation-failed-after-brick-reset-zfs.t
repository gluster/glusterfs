#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../snapshot_zfs.rc

if ! verify_zfs_version; then
    SKIP_TESTS
    exit 0;
fi

cleanup;
TEST verify_zfs_version
TEST launch_cluster 2
TEST setup_zfs 2

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $CLI_1 snapshot create ${V0}_snap1 ${V0} no-timestamp
TEST snapshot_exists 1 ${V0}_snap1

TEST $CLI_1 snapshot delete ${V0}_snap1
TEST ! snapshot_exists 1 ${V0}_snap1

TEST $CLI_1 volume reset-brick $V0 $H1:$L1 start
TEST $CLI_1 volume reset-brick $V0 $H1:$L1 $H1:$L1 commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" cluster_brick_up_status 1 $V0 $H1 $L1

TEST $CLI_1 snapshot create ${V0}_snap1 ${V0} no-timestamp
TEST snapshot_exists 1 ${V0}_snap1

TEST $CLI_1 snapshot delete ${V0}_snap1
TEST ! snapshot_exists 1 ${V0}_snap1

cleanup;
