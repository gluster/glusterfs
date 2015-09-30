#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

# Replace brick1 without killing the brick
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1_new commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST kill_brick $V0 $H0 $B0/${V0}1_new

# Replace brick1 after killing the brick
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1_new $H0:$B0/${V0}1_newer commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

cleanup;
