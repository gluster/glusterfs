#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This test verifies that self-heal fails when read/write fails as part of heal
cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST touch $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
echo abc >> $M0/a

# Umount the volume to force all pending writes to reach the bricks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Load error-gen and fail read fop and test that heal fails
TEST $CLI volume stop $V0 #Stop volume so that error-gen can be loaded
TEST $CLI volume set $V0 debug.error-gen posix
TEST $CLI volume set $V0 debug.error-fops read
TEST $CLI volume set $V0 debug.error-number EBADF
TEST $CLI volume set $V0 debug.error-failure 100

TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0
TEST ! getfattr -n trusted.ec.heal $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0

#fail write fop and test that heal fails
TEST $CLI volume stop $V0
TEST $CLI volume set $V0 debug.error-fops write

TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0
TEST ! getfattr -n trusted.ec.heal $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0

TEST $CLI volume stop $V0 #Stop volume so that error-gen can be disabled
TEST $CLI volume reset $V0 debug.error-gen
TEST $CLI volume reset $V0 debug.error-fops
TEST $CLI volume reset $V0 debug.error-number
TEST $CLI volume reset $V0 debug.error-failure

TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0
TEST getfattr -n trusted.ec.heal $M0/a
EXPECT "^0$" get_pending_heal_count $V0

#Test that heal worked as expected by forcing read from brick0
#remount to make sure data is not served from any cache
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT "abc" cat $M0/a

cleanup
