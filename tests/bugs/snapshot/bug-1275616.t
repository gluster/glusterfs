#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0
TEST $CLI snapshot config activate-on-create enable

TEST $CLI snapshot config $V0 snap-max-hard-limit 100
TEST $CLI snapshot create snap1 $V0 no-timestamp

TEST $CLI snapshot config $V0 snap-max-hard-limit 150
TEST $CLI snapshot create snap2 $V0 no-timestamp

TEST $CLI snapshot config $V0 snap-max-hard-limit 200
TEST $CLI snapshot create snap3 $V0 no-timestamp
EXPECT '197' snap_info_volume CLI "Snaps Available" $V0;

TEST $CLI volume stop $V0

# Restore the snapshots and verify the snap-max-hard-limit
# and the Snaps Available
TEST $CLI snapshot restore snap1
EXPECT '98' snap_info_volume CLI "Snaps Available" $V0;
EXPECT '100' snap_config_volume CLI 'snap-max-hard-limit' $V0

TEST $CLI snapshot restore snap2
EXPECT '149' snap_info_volume CLI "Snaps Available" $V0;
EXPECT '150' snap_config_volume CLI 'snap-max-hard-limit' $V0

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Yes" get_snap_brick_status snap3

#Take a clone and verify it inherits snapshot's snap-max-hard-limit
TEST $CLI snapshot clone clone1 snap3

EXPECT '149' snap_info_volume CLI "Snaps Available" $V0;
EXPECT '150' snap_config_volume CLI 'snap-max-hard-limit' $V0

EXPECT '200' snap_info_volume CLI "Snaps Available" clone1
EXPECT '200' snap_config_volume CLI 'snap-max-hard-limit' clone1

cleanup;
