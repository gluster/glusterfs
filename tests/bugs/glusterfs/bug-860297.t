#!/bin/bash
. $(dirname $0)/../../include.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info
TEST $CLI volume create $V0 $H0:$B0/brick1
setfattr -x trusted.glusterfs.volume-id $B0/brick1
## If Extended attribute trusted.glusterfs.volume-id is not present
## then volume should not be able to start
TEST ! $CLI volume start $V0;
cleanup;
