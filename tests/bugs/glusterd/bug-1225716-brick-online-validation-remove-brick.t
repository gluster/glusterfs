#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

#kill a brick process
kill -15 `cat $GLUSTERD_WORKDIR/vols/$V0/run/$H0-d-backends-${V0}1.pid`;

#remove-brick start should fail as the brick is down
TEST ! $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" brick_up_status $V0 $H0 $B0/${V0}1

#remove-brick start should succeed as the brick is up
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start

#kill a brick process
kill -15 `cat $GLUSTERD_WORKDIR/vols/$V0/run/$H0-d-backends-${V0}1.pid`;

#remove-brick commit should pass even if the brick is down
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 commit

cleanup;
