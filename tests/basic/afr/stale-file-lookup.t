#!/bin/bash

#This file checks if stale file lookup fails or not.
#A file is deleted when a brick was down. Before self-heal could happen to it
#the file is accessed. It should fail.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
TEST touch $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST rm -f $M0/a
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST stat $B0/${V0}0/a
TEST ! stat $B0/${V0}1/a
TEST ! ls -l $M0/a

cleanup
