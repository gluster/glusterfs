#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
cleanup;

function compare_get_split_brain_status {
        local path=$1
        local choice=$2
        echo `getfattr -n replica.split-brain-status $path` | cut -f2 -d"=" | sed -e 's/^"//'  -e 's/"$//' | grep $choice
        if [ $? -ne 0 ]
        then
                echo 1
        else
                echo 0
        fi

}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume start $V0

#Disable self-heal-daemon
TEST $CLI volume set $V0 cluster.self-heal-daemon off

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

TEST mkdir $M0/tmp1

#Create metadata split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST chmod 666 $M0/tmp1
TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

TEST chmod 757 $M0/tmp1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

EXPECT 2 get_pending_heal_count $V0


TEST kill_brick $V0 $H0 $B0/${V0}2
TEST chmod 755 $M0/tmp1
TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST chmod 766 $M0/tmp1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 3

EXPECT 4 get_pending_heal_count $V0

TEST kill_brick $V0 $H0 $B0/${V0}4
TEST chmod 765 $M0/tmp1
TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}5
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 4

TEST chmod 756 $M0/tmp1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 5

EXPECT 6 get_pending_heal_count $V0

cd $M0
EXPECT 0 compare_get_split_brain_status ./tmp1 patchy-client-0
EXPECT 0 compare_get_split_brain_status ./tmp1 patchy-client-1
EXPECT 0 compare_get_split_brain_status ./tmp1 patchy-client-2
EXPECT 0 compare_get_split_brain_status ./tmp1 patchy-client-3
EXPECT 0 compare_get_split_brain_status ./tmp1 patchy-client-4
EXPECT 0 compare_get_split_brain_status ./tmp1 patchy-client-5

cd -
cleanup
