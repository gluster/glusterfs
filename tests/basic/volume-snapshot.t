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
        $CLI_1 snapshot create ${V0}_snap ${V0} no-timestamp &
        PID_1=$!

        $CLI_1 snapshot create ${V1}_snap ${V1} no-timestamp &
        PID_2=$!

        wait $PID_1 $PID_2
}

function create_snapshots_with_timestamp() {
        $CLI_1 snapshot create ${V0}_snap1 ${V0}&
        PID_1=$!
        $CLI_1 snapshot create ${V1}_snap1 ${V1}&
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
        $CLI_1 snapshot delete $1 &
        PID_1=$!

        $CLI_1 snapshot delete $2 &
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

TEST $CLI_1 snapshot config activate-on-create enable

#Snapshot Operations
create_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

EXPECT '1' volinfo_field $V0 'Snapshot Count';
EXPECT '1' volinfo_field $V1 'Snapshot Count';
EXPECT "1" get-cmd-field-xml "volume info $V0" "snapshotCount"
EXPECT "1" get-cmd-field-xml "volume info $V1" "snapshotCount"

deactivate_snapshots

EXPECT 'Stopped' snapshot_status ${V0}_snap;
EXPECT 'Stopped' snapshot_status ${V1}_snap;

activate_snapshots

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

deactivate_snapshots
activate_snapshots

TEST snapshot_exists 1 ${V0}_snap
TEST snapshot_exists 1 ${V1}_snap
TEST $CLI_1 snapshot config $V0 snap-max-hard-limit 100
TEST $CLI_1 snapshot config $V1 snap-max-hard-limit 100

TEST glusterfs -s $H1 --volfile-id=/snaps/${V0}_snap/${V0} $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H2 --volfile-id=/snaps/${V1}_snap/${V1} $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#create timestamp appended snaps
create_snapshots_with_timestamp;
new_name1=`$CLI_1 snapshot list ${V0} | grep ${V0}_snap1`;
new_name2=`$CLI_1 snapshot list ${V1} | grep ${V1}_snap1`;

EXPECT '2' volinfo_field $V0 'Snapshot Count';
EXPECT '2' volinfo_field $V1 'Snapshot Count';
EXPECT "2" get-cmd-field-xml "volume info $V0" "snapshotCount"
EXPECT "2" get-cmd-field-xml "volume info $V1" "snapshotCount"

EXPECT_NOT "{V0}_snap1" echo $new_name1;
EXPECT_NOT "{V1}_snap1" echo $new_name1;
delete_snapshots $new_name1 $new_name2;

EXPECT '1' volinfo_field $V0 'Snapshot Count';
EXPECT '1' volinfo_field $V1 'Snapshot Count';
EXPECT "1" get-cmd-field-xml "volume info $V0" "snapshotCount"
EXPECT "1" get-cmd-field-xml "volume info $V1" "snapshotCount"

#Clean up
stop_force_volumes 2
EXPECT 'Stopped' volinfo_field $V0 'Status';
EXPECT 'Stopped' volinfo_field $V1 'Status';

restore_snapshots
TEST ! snapshot_exists 1 ${V0}_snap
TEST ! snapshot_exists 1 ${V1}_snap

EXPECT '0' volinfo_field $V0 'Snapshot Count';
EXPECT '0' volinfo_field $V1 'Snapshot Count';
EXPECT "0" get-cmd-field-xml "volume info $V0" "snapshotCount"
EXPECT "0" get-cmd-field-xml "volume info $V1" "snapshotCount"

delete_volumes 2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "N" volume_exists $V0
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "N" volume_exists $V1

cleanup;
