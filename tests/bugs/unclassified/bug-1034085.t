#!/bin/bash
#Test case: Check the creation of indices/xattrop dir as soon as brick comes up.

. $(dirname $0)/../../include.rc

cleanup;

#Create a volume
TEST glusterd;
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1};
EXPECT 'Created' volinfo_field $V0 'Status';

TEST mkdir -p $B0/${V0}-0/.glusterfs/indices/
TEST touch $B0/${V0}-0/.glusterfs/indices/xattrop

#Volume start should not work when xattrop dir not created
TEST ! $CLI volume start $V0;

TEST rm $B0/${V0}-0/.glusterfs/indices/xattrop

#Volume start should work now
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

#Check for the existence of indices/xattrop dir
TEST [ -d $B0/${V0}-0/.glusterfs/indices/xattrop/ ];

cleanup;
