#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Start and create a replicated volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1}

TEST $CLI volume set $V0 indexing on

TEST $CLI volume start $V0;

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Mount client-pid=-1
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 --client-pid=-1 $M1

TEST touch $M0

vol_uuid=$(gluster vol info $V0 | grep "Volume ID" | awk '{print $3}')

xtime="trusted.glusterfs.$vol_uuid.xtime"

#TEST xtime
TEST ! getfattr -n $xtime $M0
TEST getfattr -n $xtime $M1

#TEST stime
slave_uuid=$(uuidgen)
stime="trusted.glusterfs.$vol_uuid.$slave_uuid.stime"
TEST setfattr -n $stime -v "0xFFFE" $B0/${V0}-0
TEST setfattr -n $stime -v "0xFFFF" $B0/${V0}-1

TEST ! getfattr -n $stime $M0
TEST getfattr -n $stime $M1

cleanup;
