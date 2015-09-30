#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0


S1="${V0}-snap1"    #Create snapshot with name contains hyphen(-)
S2="-${V0}-snap2"   #Create snapshot with name starts with hyphen(-)
#Create snapshot with a long name
S3="${V0}_single_gluster_volume_is_accessible_by_multiple_clients_offline_snapshot_is_a_long_name"

TEST $CLI snapshot create $S1 $V0 no-timestamp
TEST snapshot_exists 0 $S1

TEST $CLI snapshot create $S2 $V0 no-timestamp
TEST snapshot_exists 0 $S2

TEST $CLI snapshot create $S3 $V0 no-timestamp
TEST snapshot_exists 0 $S3


TEST glusterfs -s $H0 --volfile-id=/snaps/$S1/$V0 $M0
TEST glusterfs -s $H0 --volfile-id=/snaps/$S2/$V0 $M1
TEST glusterfs -s $H0 --volfile-id=/snaps/$S3/$V0 $M2

#Clean up
#TEST $CLI snapshot delete $S1
#TEST $CLI snapshot delete $S2
#TEST $CLI snapshot delete $S3

TEST $CLI volume stop $V0 force
#TEST $CLI volume delete $V0

cleanup;
