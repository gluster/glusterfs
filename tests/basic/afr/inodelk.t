#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

#This test tests that inodelk fails when quorum is not met. Also tests the
#success case where inodelk is obtained and unlocks are done correctly.

TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0 $M0

#Test success case
TEST mkdir $M0/dir1
TEST mv $M0/dir1 $M0/dir2

#If there is a problem with inodelk unlocking the following would hang.
TEST mv $M0/dir2 $M0/dir1

#Test failure case by bringing two of the bricks down
#Test that the directory is not moved partially on some bricks but successful
#on other subvol where quorum meets. Do that for both set of bricks

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST ! mv $M0/dir1 $M0/dir2

TEST stat $B0/${V0}0/dir1
TEST stat $B0/${V0}1/dir1
TEST stat $B0/${V0}2/dir1
TEST stat $B0/${V0}3/dir1
TEST stat $B0/${V0}4/dir1
TEST stat $B0/${V0}5/dir1
TEST ! stat $B0/${V0}0/dir2
TEST ! stat $B0/${V0}1/dir2
TEST ! stat $B0/${V0}2/dir2
TEST ! stat $B0/${V0}3/dir2
TEST ! stat $B0/${V0}4/dir2
TEST ! stat $B0/${V0}5/dir2

TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST kill_brick $V0 $H0 $B0/${V0}4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST ! mv $M0/dir1 $M0/dir2
TEST stat $B0/${V0}0/dir1
TEST stat $B0/${V0}1/dir1
TEST stat $B0/${V0}2/dir1
TEST stat $B0/${V0}3/dir1
TEST stat $B0/${V0}4/dir1
TEST stat $B0/${V0}5/dir1
TEST ! stat $B0/${V0}0/dir2
TEST ! stat $B0/${V0}1/dir2
TEST ! stat $B0/${V0}2/dir2
TEST ! stat $B0/${V0}3/dir2
TEST ! stat $B0/${V0}4/dir2
TEST ! stat $B0/${V0}5/dir2

#Bring the bricks back up and try mv once more, it should succeed.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 4
TEST mv $M0/dir1 $M0/dir2
cleanup;
#Do similar tests on replica 2
TEST glusterd;
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..3}
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0 $M0
TEST mkdir $M0/dir1
TEST mv $M0/dir1 $M0/dir2
#Because we don't know hashed subvol, do the same test twice bringing 1 brick
#from each down, quorum calculation should allow it.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST mv $M0/dir2 $M0/dir1
TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST mv $M0/dir1 $M0/dir2
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST mv $M0/dir2 $M0/dir1
cleanup
