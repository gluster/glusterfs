#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TEST glusterd
TEST pidof glusterd

## Start and create a replicated volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1}

TEST $CLI volume set $V0 indexing on

TEST $CLI volume start $V0;

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Mount client-pid=-1
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 --client-pid=-1 $M1

TEST touch $M0

vol_uuid=`getfattr -n trusted.glusterfs.volume-mark -ehex $M1 | sed -n 's/^trusted.glusterfs.volume-mark=0x//p' | cut -b5-36 | sed 's/\([a-f0-9]\{8\}\)\([a-f0-9]\{4\}\)\([a-f0-9]\{4\}\)\([a-f0-9]\{4\}\)/\1-\2-\3-\4-/'`
xtime=trusted.glusterfs.$vol_uuid.xtime

TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="

TEST kill_brick $V0 $H0 $B0/${V0}-0

TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup
