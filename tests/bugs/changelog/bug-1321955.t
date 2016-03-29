#!/bin/bash

#This file checks if missing entry self-heal and entry self-heal are working
#as expected.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 changelog.changelog enable
TEST $CLI volume set $V0 changelog.capture-del-path on
TEST $CLI volume start $V0

#Mount the volume
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

#Create files
TEST touch $M0/file1
TEST mkdir $M0/dir1
TEST touch $M0/dir1/file1

#Check for presence of files
TEST stat $B0/${V0}0/dir1/file1
TEST stat $B0/${V0}1/dir1/file1
TEST stat $B0/${V0}0/file1
TEST stat $B0/${V0}1/file1

#Kill brick1
TEST kill_brick $V0 $H0 $B0/${V0}1

#Del dir1/file1
TEST rm -f $M0/dir1/file1

#file1 should be present in brick1 and not in brick0
TEST ! stat $B0/${V0}0/dir1/file1
TEST stat $B0/${V0}1/dir1/file1

#Bring up the brick which is down
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

#Initiate heal
TEST $CLI volume heal $V0

EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#dir1/file1 in brick1 should be deleted
TEST ! stat $B0/${V0}1/dir1/file1

#file1 under root should not be deleted in brick1
TEST stat $B0/${V0}1/file1

cleanup;
