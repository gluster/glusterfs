#!/bin/bash

#Test case: Hardlink test

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

#Create a file and perform fop on a DIR
TEST touch $M0/foo
TEST ls $M0/

#Create hardlink
TEST ln $M0/foo $M0/bar


EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
