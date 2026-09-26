#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../cluster.rc
. $(dirname $0)/../snapshot.rc
. $(dirname $0)/../snapshot_zfs.rc


V1="patchy2"

function create_volumes() {
        $CLI_1 volume create $V0 $H1:$L1 &
        PID_1=$!

        $CLI_2 volume create $V1 $H2:$L2 $H3:$L3 &
        PID_2=$!

        wait $PID_1 $PID_2
}

function create_snapshots() {

        $CLI_1 snapshot create $1 $2 no-timestamp&
        PID_1=$!

        wait $PID_1
}



function delete_snapshot() {
        $CLI_1 snapshot delete $1 &
        PID_1=$!

        wait $PID_1
}

# Destroy every child dataset of the brick dataset $1: the ZFS clones behind
# the cloned volumes (`zfs clone` places them under the origin dataset),
# which `volume delete` leaves behind.
function destroy_zfs_clones() {
        local ds
        for ds in $(zfs list -H -o name -t filesystem -r "$1" |
                    grep -v -x -F -- "$1"); do
                zfs destroy -f "$ds" || return 1
        done
        return 0
}

if ! verify_zfs_version; then
    SKIP_TESTS
    exit 0;
fi

cleanup;

TEST verify_zfs_version;
#Create cluster with 3 nodes
TEST launch_cluster 3;
TEST setup_zfs 3

TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;

create_volumes
EXPECT 'Created' volinfo_field_1 $V0 'Status';
EXPECT 'Created' volinfo_field_1 $V1 'Status';

start_volumes 2
EXPECT 'Started' volinfo_field_1 $V0 'Status';
EXPECT 'Started' volinfo_field_1 $V1 'Status';

TEST $CLI_1 snapshot config activate-on-create enable

#Snapshot Operations
create_snapshots ${V0}_snap ${V0};
create_snapshots ${V1}_snap ${V1};

EXPECT 'Started' snapshot_status ${V0}_snap;
EXPECT 'Started' snapshot_status ${V1}_snap;

sleep 5
TEST $CLI_1 snapshot clone ${V0}_clone ${V0}_snap
TEST $CLI_1 snapshot clone ${V1}_clone ${V1}_snap

EXPECT 'Created' volinfo_field_1 ${V0}_clone 'Status';
EXPECT 'Created' volinfo_field_1 ${V1}_clone 'Status';

TEST $CLI_1 volume start ${V0}_clone force;
TEST $CLI_1 volume start ${V1}_clone force;

EXPECT 'Started' volinfo_field_1 ${V0}_clone 'Status';
EXPECT 'Started' volinfo_field_1 ${V1}_clone 'Status';


TEST glusterfs -s $H1 --volfile-id=/${V0}_clone $M0
TEST glusterfs -s $H2 --volfile-id=/${V1}_clone $M1

TEST touch $M0/file1
TEST touch $M1/file1

TEST echo "Hello world" $M0/file1
TEST echo "Hello world" $M1/file1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST kill_glusterd 2;
sleep 15
TEST $glusterd_2;
sleep 15

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field_1 ${V0}_clone 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field_1 ${V1}_clone 'Status';

TEST $CLI_1 volume stop ${V0}_clone
TEST $CLI_1 volume stop ${V1}_clone

TEST $CLI_1 volume delete ${V0}_clone
TEST $CLI_1 volume delete ${V1}_clone

TEST $CLI_1 snapshot clone ${V0}_clone ${V0}_snap
TEST $CLI_1 snapshot clone ${V1}_clone ${V1}_snap

EXPECT 'Created' volinfo_field_1 ${V0}_clone 'Status';
EXPECT 'Created' volinfo_field_1 ${V1}_clone 'Status';

#Clean up
stop_force_volumes 2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT 'Stopped' volinfo_field_1 $V0 'Status';
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT 'Stopped' volinfo_field_1 $V1 'Status';

# ${V0}_clone and ${V1}_clone are ZFS clones of ${V0}_snap and ${V1}_snap
# (`zfs clone`, never promoted), so the snapshots cannot be deleted while the
# clones exist: the delete is refused up front and nothing changes
# (gluster/glusterfs#4689; glusterd used to report success, drop its snapshot
# object and leave the backend snapshot behind).
TEST ! delete_snapshot ${V0}_snap
TEST ! delete_snapshot ${V1}_snap
TEST snapshot_exists 1 ${V0}_snap
TEST snapshot_exists 1 ${V1}_snap

# Release the dependents: delete the clone volumes and destroy their backend
# datasets, which `volume delete` leaves behind (both clone rounds left one
# under each brick dataset; no brick serves them any more).
TEST $CLI_1 volume delete ${V0}_clone
TEST $CLI_1 volume delete ${V1}_clone
TEST destroy_zfs_clones ${ZFS_PREFIX}_pool_1/bricks
TEST destroy_zfs_clones ${ZFS_PREFIX}_pool_2/bricks
TEST destroy_zfs_clones ${ZFS_PREFIX}_pool_3/bricks

# with no dependent left the delete goes through
TEST delete_snapshot ${V0}_snap
TEST delete_snapshot ${V1}_snap

TEST ! snapshot_exists 1 ${V0}_snap
TEST ! snapshot_exists 1 ${V1}_snap

delete_volumes 2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "N" volume_exists_1 $V0
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "N" volume_exists_1 $V1

cleanup;
