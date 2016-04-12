#!/bin/bash

. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../include.rc
cleanup;

TESTS_EXPECTED_IN_LOOP=2

TEST verify_lvm_version
TEST init_n_bricks 2
TEST setup_lvm 2

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$L1
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume create $V1 $H0:$L2
EXPECT 'Created' volinfo_field $V1 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $CLI volume start $V1
EXPECT 'Started' volinfo_field $V1 'Status'

TEST $CLI snapshot create ${V0}_snap $V0 no-timestamp
TEST $CLI snapshot create ${V1}_snap $V1 no-timestamp

# Simulate a node reboot by unmounting the brick, snap_brick and followed by
# deleting the brick. Now once glusterd restarts, it should be able to construct
# and remount the snap brick
snap_bricks=`gluster snap status | grep "Brick Path" | awk -F ":"  '{print $3}'`

TEST $CLI volume stop $V1
TEST $CLI snapshot restore ${V1}_snap;

pkill gluster
for snap_brick in $snap_bricks
do
    echo "Unmounting snap brick" $snap_brick
    EXPECT_WITHIN_TEST_IN_LOOP $UMOUNT_TIMEOUT "Y" force_umount $snap_brick
done

rm -rf $snap_brick

TEST glusterd
TEST pidof glusterd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $L1

cleanup

