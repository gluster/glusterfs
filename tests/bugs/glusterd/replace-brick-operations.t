#!/bin/bash

## Test case for BZ: 1094119  Remove replace-brick support from gluster

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Start glusterd
TEST glusterd
TEST pidof glusterd

## Lets create and start volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}
TEST $CLI volume start $V0

#bug-1094119-remove-replace-brick-support-from-glusterd

## Now with this patch replace-brick only accept following commad
## volume replace-brick <VOLNAME> <SOURCE-BRICK> <NEW-BRICK> {commit force}
## Apart form this replace brick command will failed.

TEST ! $CLI volume replace-brick $V0 $H0:$B0/${V0}2 $H0:$B0/${V0}3 start
TEST ! $CLI volume replace-brick $V0 $H0:$B0/${V0}2 $H0:$B0/${V0}3 status
TEST ! $CLI volume replace-brick $V0 $H0:$B0/${V0}2 $H0:$B0/${V0}3 abort


## replace-brick commit force command should success
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}2 $H0:$B0/${V0}3 commit force

#bug-1242543-replace-brick validation

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

# Replace brick1 without killing
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1_new commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST kill_brick $V0 $H0 $B0/${V0}1_new

# Replace brick1 after killing the brick
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1_new $H0:$B0/${V0}1_newer commit force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

cleanup;
