#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../snapshot_zfs.rc
. $(dirname $0)/../../volume.rc

if ! verify_zfs_version; then
    SKIP_TESTS
    exit 0;
fi

cleanup;
TEST verify_zfs_version;
TEST glusterd;
TEST pidof glusterd;

TEST init_n_bricks 3
TEST setup_zfs 3

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0
TEST $CLI volume quota $V0 enable

TEST $CLI snapshot create snap1 $V0 no-timestamp
TEST $CLI snapshot activate snap1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Yes" get_snap_brick_status snap1

#Take a clone and verify it inherits snapshot's snap-max-hard-limit
TEST $CLI snapshot clone clone1 snap1
TEST $CLI volume start clone1
EXPECT 'Started' volinfo_field clone1 'Status';

cleanup;
