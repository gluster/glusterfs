#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;
TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume start $V0;
TEST $CLI volume set $V0 features.uss enable;
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

mkdir $M0/test
echo "file1" > $M0/file1
ln -s $M0/file1 $M0/test/file_symlink
ls -l $M0/ > /dev/null
ls -l $M0/test/ > /dev/null

TEST $CLI snapshot create snap1 $V0 no-timestamp;
$CLI snapshot activate snap1;
EXPECT 'Started' snapshot_status snap1;

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" snap_client_connected_status $V0
ls $M0/.snaps/snap1/test/ > /dev/null
ls -l $M0/.snaps/snap1/test/ > /dev/null
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" snap_client_connected_status $V0

TEST $CLI snapshot delete snap1;
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
