#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;
TEST glusterd;

TEST $CLI volume create $V0 replica 3 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume start $V0;

TEST kill_brick $V0 $H0 $L1;

#Normal snap create should fail
TEST !  $CLI snapshot create ${V0}_snap1 $V0;
TEST !  snapshot_exists 0 ${V0}_snap1;

#Force snap create should succeed
TEST  $CLI snapshot create ${V0}_snap1 $V0 force;
TEST  snapshot_exists 0 ${V0}_snap1;

#Delete the created snap
TEST  $CLI snapshot delete ${V0}_snap1;
TEST  ! snapshot_exists 0 ${V0}_snap1;

cleanup;
