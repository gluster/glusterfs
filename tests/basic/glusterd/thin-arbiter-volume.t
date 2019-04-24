#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../ volume.rc
. $(dirname $0)/../../thin-arbiter.rc

#This command tests the volume create command validation for thin-arbiter volumes.

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 thin-arbiter 1 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
EXPECT "1 x 2 = 2" volinfo_field $V0 "Number of Bricks"
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST touch $M0/a.txt
TEST ls $B0/b1/a.txt 
TEST ls $B0/b2/a.txt 
TEST ! ls $B0/b3/a.txt

TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

TEST $CLI volume create $V0 replica 2 thin-arbiter 1 $H0:$B0/b{4..8}
EXPECT "2 x 2 = 4" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0

TEST rm -rf $B0/b{1..3}

TEST $CLI volume create $V0 replica 2 thin-arbiter 1 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
EXPECT "1 x 2 = 2" volinfo_field $V0 "Number of Bricks"

TEST killall -15 glusterd
TEST glusterd
TEST pidof glusterd
EXPECT "1 x 2 = 2" volinfo_field $V0 "Number of Bricks"

cleanup

