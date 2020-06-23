#!/bin/bash

SCRIPT_TIMEOUT=250

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

cleanup

TEST glusterd
TEST pidof glusterd

BRICK1=$B0/${V0}-0
BRICK2=$B0/${V0}-1

TEST $CLI volume create $V0 $H0:$BRICK1 $H0:$BRICK2
TEST $CLI volume start $V0

TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST mkdir $M0/dir

#remove dir from one of the brick
TEST rmdir $BRICK2/dir

#safe cache timeout for lookup to be triggered
sleep 2

TEST ls $M0/dir

TEST stat $BRICK2/dir

cleanup
