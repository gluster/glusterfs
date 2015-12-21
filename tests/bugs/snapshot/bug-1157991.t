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

TEST $CLI snapshot create snap1 $V0 no-timestamp
EXPECT 'Stopped' snapshot_status snap1;

TEST $CLI snapshot config activate-on-create enable
TEST $CLI snapshot create snap2 $V0 no-timestamp
EXPECT 'Started' snapshot_status snap2;

#Clean up
TEST $CLI snapshot delete snap1
TEST $CLI snapshot delete snap2

TEST $CLI volume stop $V0 force
TEST $CLI volume delete $V0

cleanup;
