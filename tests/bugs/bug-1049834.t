#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../cluster.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc

cleanup;

TEST launch_cluster 2
TEST setup_lvm 2

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN 20 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

#Setting the snap-max-hard-limit to 4
TEST $CLI_1 snapshot config $V0 snap-max-hard-limit 4
PID_1=$!
wait $PID_1

#Creating 4 snapshots on the volume
TEST create_n_snapshots $V0 4 $V0_snap
TEST snapshot_n_exists $V0 4 $V0_snap

#Creating the 5th snapshots on the volume and expecting it not to be created.
TEST ! $CLI_1 snapshot create ${V0}_snap5 ${V0}
TEST ! snapshot_exists 1 ${V0}_snap5
TEST ! $CLI_1 snapshot delete ${V0}_snap5

#Deleting the 4 snaps
#TEST delete_n_snapshots $V0 4 $V0_snap
#TEST ! snapshot_n_exists $V0 4 $V0_snap

cleanup;
