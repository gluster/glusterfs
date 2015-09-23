#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V00}{1..2};
TEST $CLI volume start $V0

#Setting data-self-heal option on for distribute volume
TEST ! $CLI volume set $V0 data-self-heal on
EXPECT '' volinfo_field $V0 'cluster.data-self-heal';
TEST ! $CLI volume set $V0 cluster.data-self-heal on
EXPECT '' volinfo_field $V0 'cluster.data-self-heal';

#Setting metadata-self-heal option on for distribute volume
TEST ! $CLI volume set $V0 metadata-self-heal on
EXPECT '' volinfo_field $V0 'cluster.metadata-self-heal';
TEST ! $CLI volume set $V0 cluster.metadata-self-heal on
EXPECT '' volinfo_field $V0 'cluster.metadata-self-heal';

#Setting entry-self-heal option on for distribute volume
TEST ! $CLI volume set $V0 entry-self-heal on
EXPECT '' volinfo_field $V0 'cluster.entrydata-self-heal';
TEST ! $CLI volume set $V0 cluster.entry-self-heal on
EXPECT '' volinfo_field $V0 'cluster.entrydata-self-heal';

#Delete the volume
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;


#Create a distribute-replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4};
TEST $CLI volume start $V0

#Setting data-self-heal option on for distribute-replicate volume
TEST $CLI volume set $V0 data-self-heal on
EXPECT 'on' volinfo_field $V0 'cluster.data-self-heal';
TEST $CLI volume set $V0 cluster.data-self-heal on
EXPECT 'on' volinfo_field $V0 'cluster.data-self-heal';

#Setting metadata-self-heal option on for distribute-replicate volume
TEST $CLI volume set $V0 metadata-self-heal on
EXPECT 'on' volinfo_field $V0 'cluster.metadata-self-heal';
TEST $CLI volume set $V0 cluster.metadata-self-heal on
EXPECT 'on' volinfo_field $V0 'cluster.metadata-self-heal';

#Setting entry-self-heal option on for distribute-replicate volume
TEST $CLI volume set $V0 entry-self-heal on
EXPECT 'on' volinfo_field $V0 'cluster.entry-self-heal';
TEST $CLI volume set $V0 cluster.entry-self-heal on
EXPECT 'on' volinfo_field $V0 'cluster.entry-self-heal';

#Delete the volume
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;


cleanup;
