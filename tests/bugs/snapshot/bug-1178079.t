#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0;

TEST $CLI volume set $V0 features.uss on;

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST touch $M0/file;

TEST getfattr -d -m . -e hex $M0/file;

cleanup;
