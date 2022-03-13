#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../snapshot_zfs.rc

if ! verify_zfs_version; then
    SKIP_TESTS
    exit 0;
fi

cleanup;

TEST init_n_bricks 1;
TEST setup_zfs 1;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1;

TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

TEST $CLI snapshot create snap1 $V0 no-timestamp;

EXPECT "description" get-cmd-field-xml "snapshot info snap1" "description"

cleanup  ;
