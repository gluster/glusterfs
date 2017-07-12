#!/bin/bash

#This test checks that gfid/volume-id removexattrs are not allowed.
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

TEST ! setfattr -x trusted.gfid $M0
TEST ! setfattr -x trusted.glusterfs.volume-id $M0
TEST setfattr -n trusted.abc -v abc $M0
TEST setfattr -x trusted.abc $M0

cleanup;
