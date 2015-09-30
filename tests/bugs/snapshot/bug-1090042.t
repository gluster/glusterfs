#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;
TEST glusterd;

TEST $CLI volume create $V0 replica 3 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume start $V0;

TEST kill_brick $V0 $H0 $L1;

#Normal snap create should fail
TEST !  $CLI snapshot create ${V0}_snap1 $V0 no-timestamp;
TEST !  snapshot_exists 0 ${V0}_snap1;

#With changes introduced in BZ #1184344 force snap create should fail too
TEST  ! $CLI snapshot create ${V0}_snap1 $V0 no-timestamp force;
TEST  ! snapshot_exists 0 ${V0}_snap1;

cleanup;
