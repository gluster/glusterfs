#!/bin/bash

## Test case for BZ: 1094119  Remove replace-brick support from gluster

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Start glusterd
TEST glusterd
TEST pidof glusterd

## Lets create and start volume
TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

## Now with this patch replace-brick only accept following commad
## volume replace-brick <VOLNAME> <SOURCE-BRICK> <NEW-BRICK> {commit force}
## Apart form this replace brick command will failed.

TEST ! $CLI volume replace-brick $V0 $H0:$B0/brick1 $H0:$B0/brick3 start
TEST ! $CLI volume replace-brick $V0 $H0:$B0/brick1 $H0:$B0/brick3 status
TEST ! $CLI volume replace-brick $V0 $H0:$B0/brick1 $H0:$B0/brick3 abort


## replace-brick commit force command should success
TEST $CLI volume replace-brick $V0 $H0:$B0/brick1 $H0:$B0/brick3 commit force

cleanup;
