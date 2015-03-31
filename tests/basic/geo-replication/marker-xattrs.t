#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TEST glusterd
TEST pidof glusterd

## Start and create a replicated volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1,2,3}

TEST $CLI volume set $V0 indexing on

TEST $CLI volume start $V0;

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Mount client-pid=-1
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 --client-pid=-1 $M1

TEST touch $M0

vol_uuid=$(get_volume_mark $M1)
xtime=trusted.glusterfs.$vol_uuid.xtime

TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="

TEST kill_brick $V0 $H0 $B0/${V0}-0

TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="

TEST getfattr -d -m. -e hex $M1
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup
TEST glusterd
TEST pidof glusterd
## Start and create a disperse volume
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}-{0,1,2}

TEST $CLI volume set $V0 indexing on

TEST $CLI volume start $V0;

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" mount_get_option_value $M0 $V0-disperse-0 childs_up

## Mount client-pid=-1
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 --client-pid=-1 $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" mount_get_option_value $M1 $V0-disperse-0 childs_up

TEST touch $M0

vol_uuid=$(get_volume_mark $M1)
xtime=trusted.glusterfs.$vol_uuid.xtime
stime=trusted.glusterfs.$vol_uuid.stime

stime_val=$(getfattr -e hex -n $xtime $M1 | grep ${xtime}= | cut -f2 -d'=')
TEST "setfattr -n $stime -v $stime_val $B0/${V0}-1"
TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="

TEST kill_brick $V0 $H0 $B0/${V0}-0

TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="
TEST "getfattr -n $stime $M1 | grep -q ${stime}="

TEST getfattr -d -m. -e hex $M1
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup
TEST glusterd
TEST pidof glusterd
## Start and create a stripe volume
TEST $CLI volume create $V0 stripe 2 $H0:$B0/${V0}-{0,1}

TEST $CLI volume set $V0 indexing on

TEST $CLI volume start $V0;

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Mount client-pid=-1
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 --client-pid=-1 $M1

TEST touch $M0

vol_uuid=$(get_volume_mark $M1)
xtime=trusted.glusterfs.$vol_uuid.xtime

TEST "getfattr -n $xtime $M1 | grep -q ${xtime}="

TEST kill_brick $V0 $H0 $B0/${V0}-0

#Stripe doesn't tolerate ENOTCONN
TEST ! "getfattr -n $xtime $M1 | grep -q ${xtime}="

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup
