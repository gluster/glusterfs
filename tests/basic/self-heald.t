#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}4
cd $M0
HEAL_FILES=0
for i in {1..10}
do
        dd if=/dev/urandom of=f bs=1024k count=10 2>/dev/null
        HEAL_FILES=$(($HEAL_FILES+1))
        mkdir a; cd a;
        HEAL_FILES=$(($HEAL_FILES+3)) #As many times as distribute subvols
done
HEAL_FILES=$(($HEAL_FILES + 3)) #Count the brick root dir

cd ~
EXPECT "$HEAL_FILES" afr_get_pending_heal_count $V0
TEST ! $CLI volume heal $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST ! $CLI volume heal $V0 info
TEST ! $CLI volume heal $V0
TEST $CLI volume start $V0 force
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 4

TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "0" afr_get_pending_heal_count $V0
cleanup
