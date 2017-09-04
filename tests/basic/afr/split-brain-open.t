#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

#Disable self-heal-daemon
TEST $CLI volume heal $V0 disable

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

TEST touch $M0/data-split-brain.txt

#Create data split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0

`echo "brick1_alive" > $M0/data-split-brain.txt`
TEST [ $? == 0 ];

TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

`echo "brick0_alive" > $M0/data-split-brain.txt`
TEST [ $? == 0 ];

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

echo "all-alive" >> $M0/data-split-brain.txt
TEST [ $? != 0 ];

cleanup;
