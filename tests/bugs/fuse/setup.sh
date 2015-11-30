#!/bin/bash

. $(dirname $0)/../../include.rc
#. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE without selinux:
TEST glusterfs -s $H0 --volfile-id $V0 $@ $M0

echo "Gluster started and volume '$V0' mounted under '$M0'"
