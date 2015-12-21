#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 ensure-durability off
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}4
cd $M0
HEAL_FILES=0
for i in {1..10}
do
        echo "abc" > $i
        HEAL_FILES=$(($HEAL_FILES+1))
done
HEAL_FILES=$(($HEAL_FILES+3)) #count brick root distribute-subvol num of times

cd ~
EXPECT "$HEAL_FILES" get_pending_heal_count $V0
TEST rm -f $M0/*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume heal $V0 info
#Only root dir should be present now in the indices
EXPECT "1" afr_get_num_indices_in_brick $B0/${V0}1
EXPECT "1" afr_get_num_indices_in_brick $B0/${V0}3
EXPECT "1" afr_get_num_indices_in_brick $B0/${V0}5
cleanup
