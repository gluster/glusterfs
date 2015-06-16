#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version;
TEST glusterd -LDEBUG;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1/brick_dir
TEST $CLI volume start $V0

TEST $CLI snapshot create snap1 $V0 no-timestamp

TEST $CLI snapshot delete snap1

TEST $CLI volume stop $V0 force
TEST $CLI volume delete $V0
cleanup
