#!/bin/bash

. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function create_snapshots() {
        $CLI_1 snapshot create ${V0}_snap ${V0} no-timestamp &
        PID_1=$!

        $CLI_1 snapshot create ${V1}_snap ${V1} no-timestamp &
        PID_2=$!

        wait $PID_1 $PID_2
}

function activate_snapshots() {
        $CLI_1 snapshot activate ${V0}_snap &
        PID_1=$!

        $CLI_1 snapshot activate ${V1}_snap &
        PID_2=$!

        wait $PID_1 $PID_2
}

function deactivate_snapshots() {
        $CLI_1 snapshot deactivate ${V0}_snap &
        PID_1=$!

        $CLI_1 snapshot deactivate ${V1}_snap &
        PID_2=$!

        wait $PID_1 $PID_2
}
cleanup;

TEST verify_lvm_version;
# Create cluster with 3 nodes
TEST launch_cluster 3;
TEST setup_lvm 3

TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;

# Create volumes
TEST $CLI_1 volume create $V0 $H1:$L1
TEST $CLI_2 volume create $V1 $H2:$L2 $H3:$L3

# Start volumes
TEST $CLI_1 volume start $V0
TEST $CLI_2 volume start $V1

TEST $CLI_1 snapshot config activate-on-create enable

# Snapshot Operations
create_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

deactivate_snapshots

EXPECT 'Stopped' snapshot_status ${V0}_snap;
EXPECT 'Stopped' snapshot_status ${V1}_snap;

activate_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

# This Function will get snap id form snap info command and will
# check for mount point in system against snap id.
function mounted_snaps
{
        snap_id=`$CLI_1 snap info $1_snap | grep "Snap Volume Name" |
                  awk -F ":" '{print $2}'`
        echo `mount | grep $snap_id | wc -l`
}

EXPECT "1" mounted_snaps ${V0}
EXPECT "2" mounted_snaps ${V1}

deactivate_snapshots

EXPECT "0" mounted_snaps ${V0}
EXPECT "0" mounted_snaps ${V1}

# This part of test is designed to validate that updates are properly being
# handled during handshake.

activate_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

kill_glusterd 2

deactivate_snapshots
EXPECT 'Stopped' snapshot_status ${V0}_snap;
EXPECT 'Stopped' snapshot_status ${V1}_snap;

TEST start_glusterd 2

# Updates form friend should reflect as snap was deactivated while glusterd
# process was inactive and mount point should also not exist.

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" mounted_snaps ${V0}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" mounted_snaps ${V1}

kill_glusterd 2
activate_snapshots
EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;
TEST start_glusterd 2

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;

# Updates form friend should reflect as snap was activated while glusterd
# process was inactive and mount point should exist.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" mounted_snaps ${V0}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" mounted_snaps ${V1}

cleanup;
