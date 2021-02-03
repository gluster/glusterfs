#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
TEST $CLI volume set $V0 cluster.lookup-optimize off
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

#Create files
TEST mkdir $M0/foo
TEST touch $M0/foo/a

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}2

TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST setfattr -n distribute.fix.layout -v "yes" $M0
TEST setfattr -n distribute.fix.layout -v "yes" $M0/foo
TEST ! setfattr -n distribute.fix.layout -v "yes" $M0/foo/a
TEST ls $M0 #Test that the mount didn't crash

cleanup;
