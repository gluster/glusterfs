#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version
TEST launch_cluster 2
TEST setup_lvm 2

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

#Setting the snap-max-hard-limit to 4
TEST $CLI_1 snapshot config $V0 snap-max-hard-limit 4
PID_1=$!
wait $PID_1

#Creating 3 snapshots on the volume (which is the soft-limit)
TEST create_n_snapshots $V0 3 $V0_snap
TEST snapshot_n_exists $V0 3 $V0_snap

#Creating the 4th snapshot on the volume and expecting it to be created
# but with the deletion of the oldest snapshot i.e 1st snapshot
TEST  $CLI_1 snapshot create ${V0}_snap4 ${V0} no-timestamp
TEST  snapshot_exists 1 ${V0}_snap4
TEST ! snapshot_exists 1 ${V0}_snap1
TEST $CLI_1 snapshot delete ${V0}_snap4
TEST $CLI_1 snapshot create ${V0}_snap1 ${V0} no-timestamp
TEST snapshot_exists 1 ${V0}_snap1

#Deleting the 4 snaps
#TEST delete_n_snapshots $V0 4 $V0_snap
#TEST ! snapshot_n_exists $V0 4 $V0_snap

cleanup;
