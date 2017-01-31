#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks notify part of ec

# We *know* some of these mounts will succeed but not be actually usable
# (terrible idea IMO), so speed things up and eliminate some noise by
# overriding this function.
_GFS () {
	glusterfs "$@"
}

ec_up_brick_count () {
	local bricknum
	for bricknum in $(seq 0 2); do
		brick_up_status $V0 $H0 $B0/$V0$bricknum
	done | grep -E '^1$' | wc -l
}

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" ec_up_brick_count

#First time mount tests.
# When all the bricks are up, mount should succeed and up-children
# count should be 3
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST stat $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# When the volume is stopped mount succeeds and up-children will be 0
TEST $CLI volume stop $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
# Wait for 5 seconds even after that up_count should show 0
sleep 5;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "0" ec_child_up_count $V0 0
TEST ! stat $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# When 2 bricks are up, mount should succeed and up-children
# count should be 2

TEST $CLI volume start $V0
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" ec_up_brick_count
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
TEST stat $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# When only 1 brick is up mount should fail.
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ec_up_brick_count
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
# Wait for 5 seconds even after that up_count should show 1
sleep 5
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_count $V0 0
TEST ! stat $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Mount already succeeded. Test that the brick up down are leading to correct
# state changes in ec.
TEST $CLI volume stop $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" ec_up_brick_count
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST touch $M0/a

# kill 1 brick and the up_count should become 2, fops should still succeed
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" ec_up_brick_count
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
TEST touch $M0/b

# kill one more brick and the up_count should become 1, fops should fail
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ec_up_brick_count
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" ec_child_up_count $V0 0
TEST ! touch $M0/c

# kill one more brick and the up_count should become 0, fops should still fail
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" ec_up_brick_count
EXPECT_WITHIN $CHILD_UP_TIMEOUT "0" ec_child_up_count $V0 0
TEST ! touch $M0/c

# Bring up all the bricks up and see that up_count is 3 and fops are succeeding
# again.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "3" ec_up_brick_count
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST touch $M0/c

cleanup
