#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST init_n_bricks 2
TEST setup_lvm 2
TEST glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2
TEST $CLI volume start $V0

# enable uss and mount the volume
TEST $CLI volume set $V0 features.uss enable
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0

# create 10 snapshots and check if all are being reflected
# in the USS world
gluster snapshot config activate-on-create enable
for i in {1..10}; do $CLI snapshot create snap$i $V0 no-timestamp; done
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 10 uss_count_snap_displayed $M0

# snapshots should not be displayed after deactivation
for i in {1..10}; do $CLI snapshot deactivate snap$i --mode=script; done
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 uss_count_snap_displayed $M0

# activate all the snapshots and check if all the activated snapshots
# are displayed again
for i in {1..10}; do $CLI snapshot activate snap$i --mode=script; done
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 10 uss_count_snap_displayed $M0

cleanup;

