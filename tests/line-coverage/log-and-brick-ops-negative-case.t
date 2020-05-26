#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup
TEST glusterd
TEST pidof glusterd

#create volumes
TEST $CLI volume create ${V0}_1 $H0:$B0/v{1..2}

TEST $CLI volume create ${V0}_2 replica 3 arbiter 1 $H0:$B0/v{3..5}

TEST $CLI volume create ${V0}_3 disperse 3 redundancy 1 $H0:$B0/v{6..8}
TEST $CLI volume start ${V0}_3
EXPECT 'Started' volinfo_field ${V0}_3 'Status'

TEST $CLI volume create ${V0}_4 replica 3 $H0:$B0/v{9..14}
TEST $CLI volume start ${V0}_4
EXPECT 'Started' volinfo_field ${V0}_4 'Status'

#log rotate option
#provided volume does not exist
TEST ! $CLI volume log ${V0}_5 rotate

#volume must be started before using log rotate option
TEST ! $CLI volume log ${V0}_1 rotate
TEST $CLI volume start ${V0}_1
EXPECT 'Started' volinfo_field ${V0}_1 'Status'

#incorrect brick provided for the volume
TEST ! $CLI volume log ${V0}_1 rotate $H0:$B0/v15

#add-brick operations
#volume must be in started to state to increase replica count
TEST ! $CLI volume add-brick ${V0}_2 replica 4 $H0:$B0/v15
TEST $CLI volume start ${V0}_2
EXPECT 'Started' volinfo_field ${V0}_2 'Status'

#incorrect number of bricks for a replica 4 volume
TEST ! $CLI volume add-brick ${V0}_1 replica 4 $H0:$B0/v15

#replica count provided is less than the current replica count
TEST ! $CLI volume add-brick ${V0}_2 replica 2 $H0:$B0/v15

#dispersed to replicated dispersed not possible
TEST ! $CLI volume add-brick ${V0}_3 replica 2 $H0:$B0/v15

#remove-brick operations
#replica count option provided for dispersed vol
TEST ! $CLI volume remove-brick ${V0}_3 replica 2 $H0:$B0/v8 start

#given replica count is greater than the current replica count
TEST ! $CLI volume remove-brick ${V0}_2 replica 4 $H0:$B0/v5 start

#number of bricks to be removed, must be a multiple of replica count
TEST ! $CLI volume remove-brick ${V0}_2 replica 3 $H0:$B0/v{3..4} start

#less number of bricks given to reduce the replica count
TEST ! $CLI volume remove-brick ${V0}_2 replica 1 $H0:$B0/v3 start

#bricks should be from different subvol
TEST ! $CLI volume remove-brick ${V0}_4 replica 2 $H0:$B0/v{13..14} start

#arbiter must be removed to reduce replica count
TEST ! $CLI volume remove-brick ${V0}_2 replica 1 $H0:$B0/v{3..4} start

#removal of bricks is not allowed without reducing the replica count explicitly
TEST ! $CLI volume remove-brick ${V0}_2 replica 3 $H0:$B0/v{3..5} start

#incorrect brick for given vol
TEST ! $CLI volume  remove-brick ${V0}_1 $H0:$B0/v15 start

#removing all the bricks are not allowed
TEST ! $CLI volume remove-brick ${V0}_1 $H0:$B0/v{1..2} start

#volume must not be stopped state while removing bricks
TEST $CLI volume stop ${V0}_1
TEST ! $CLI volume remove-brick ${V0}_1 $H0:$B0/v1 start

cleanup