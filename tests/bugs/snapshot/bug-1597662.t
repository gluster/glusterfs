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

snap_path=/var/run/gluster/snaps

TEST $CLI snapshot create snap1 $V0 no-timestamp;

$CLI snapshot activate snap1;

EXPECT 'Started' snapshot_status snap1;

# This Function will check for entry /var/run/gluster/snaps/<snap-name>
# against snap-name

function is_snap_path
{
        echo `ls $snap_path | grep snap1 | wc -l`
}

# snap is active so snap_path should exist
EXPECT "1" is_snap_path

$CLI snapshot deactivate snap1;
EXPECT_WITHIN ${PROCESS_DOWN_TIMEOUT} 'Stopped' snapshot_status snap1
# snap is deactivated so snap_path should not exist
EXPECT "0" is_snap_path

# activate snap again
$CLI snapshot activate snap1;
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} 'Started' snapshot_status snap1

# snap is active so snap_path should exist
EXPECT "1" is_snap_path

# delete snap now
TEST $CLI snapshot delete snap1;

# snap is deleted so snap_path should not exist
EXPECT "0" is_snap_path

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;

