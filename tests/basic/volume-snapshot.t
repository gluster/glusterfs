#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../cluster.rc
. $(dirname $0)/../snapshot.rc


V1="patchy2"

function create_volumes() {
        $CLI_1 volume create $V0 $H1:$L1 &
        PID_1=$!

        $CLI_2 volume create $V1 $H2:$L2 $H3:$L3 &
        PID_2=$!

        wait $PID_1 $PID_2
}

function create_snapshots() {
        $CLI_1 snapshot create ${V0}_snap ${V0}&
        PID_1=$!

        $CLI_1 snapshot create ${V1}_snap ${V1}&
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

function delete_snapshots() {
        $CLI_1 snapshot delete ${V0}_snap &
        PID_1=$!

        $CLI_1 snapshot delete ${V1}_snap &
        PID_2=$!

        wait $PID_1 $PID_2
}

function restore_snapshots() {
        $CLI_1 snapshot restore ${V0}_snap &
        PID_1=$!

        $CLI_1 snapshot restore ${V1}_snap &
        PID_2=$!

        wait $PID_1 $PID_2
}
cleanup;

TEST verify_lvm_version;
#Create cluster with 3 nodes
TEST launch_cluster 3;
TEST setup_lvm 3

TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;

create_volumes
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Created' volinfo_field $V1 'Status';

start_volumes 2
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT 'Started' volinfo_field $V1 'Status';

#Snapshot Operations
create_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

deactivate_snapshots

EXPECT 'Stopped' snapshot_status ${V0}_snap;
EXPECT 'Stopped' snapshot_status ${V1}_snap;

activate_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

TEST snapshot_exists 1 ${V0}_snap
TEST snapshot_exists 1 ${V1}_snap
TEST $CLI_1 snapshot config $V0 snap-max-hard-limit 100
TEST $CLI_1 snapshot config $V1 snap-max-hard-limit 100

TEST glusterfs -s $H1 --volfile-id=/snaps/${V0}_snap/${V0} $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H2 --volfile-id=/snaps/${V1}_snap/${V1} $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Clean up
stop_force_volumes 2
EXPECT 'Stopped' volinfo_field $V0 'Status';
EXPECT 'Stopped' volinfo_field $V1 'Status';

restore_snapshots
TEST ! snapshot_exists 1 ${V0}_snap
TEST ! snapshot_exists 1 ${V1}_snap

delete_volumes 2
TEST ! volume_exists $V0
TEST ! volume_exists $V1

cleanup;
