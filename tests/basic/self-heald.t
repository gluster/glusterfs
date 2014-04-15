#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

function disconnected_brick_count {
        local vol=$1
        $CLI volume heal $vol info | grep -i transport | wc -l
}

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
        dd if=/dev/urandom of=f bs=1M count=10 2>/dev/null
        HEAL_FILES=$(($HEAL_FILES+1))
        mkdir a; cd a;
        HEAL_FILES=$(($HEAL_FILES+3)) #As many times as distribute subvols
done
HEAL_FILES=$(($HEAL_FILES + 3)) #Count the brick root dir

cd ~
EXPECT "$HEAL_FILES" afr_get_pending_heal_count $V0

#When bricks are down, it says Transport End point Not connected for them
EXPECT "3" disconnected_brick_count $V0

#Create some stale indices and verify that they are not counted in heal info
#TO create stale index create and delete files when one brick is down in
#replica pair.
for i in {11..20}; do echo abc > $M0/$i; done
HEAL_FILES=$(($HEAL_FILES + 10)) #count extra 10 files
EXPECT "$HEAL_FILES" afr_get_pending_heal_count $V0
#delete the files now, so that stale indices will remain.
for i in {11..20}; do rm -f $M0/$i; done
#After deleting files they should not appear in heal info
HEAL_FILES=$(($HEAL_FILES - 10))
EXPECT "$HEAL_FILES" afr_get_pending_heal_count $V0


TEST ! $CLI volume heal $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST ! $CLI volume heal $V0
TEST ! $CLI volume heal $V0 full
TEST $CLI volume start $V0 force
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN 20 "Y" glustershd_up_status
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 4
TEST $CLI volume heal $V0
sleep 5 #Until the heal-statistics command implementation
#check that this heals the contents partially
TEST [ $HEAL_FILES -gt $(afr_get_pending_heal_count $V0) ]

TEST $CLI volume heal $V0 full
EXPECT_WITHIN 30 "0" afr_get_pending_heal_count $V0

#Test that ongoing IO is not considered as Pending heal
(dd if=/dev/zero of=$M0/file1 bs=1K 2>/dev/null 1>/dev/null)&
back_pid1=$!;
(dd if=/dev/zero of=$M0/file2 bs=1K 2>/dev/null 1>/dev/null)&
back_pid2=$!;
(dd if=/dev/zero of=$M0/file3 bs=1K 2>/dev/null 1>/dev/null)&
back_pid3=$!;
(dd if=/dev/zero of=$M0/file4 bs=1K 2>/dev/null 1>/dev/null)&
back_pid4=$!;
(dd if=/dev/zero of=$M0/file5 bs=1K 2>/dev/null 1>/dev/null)&
back_pid5=$!;
EXPECT 0 afr_get_pending_heal_count $V0
kill -SIGTERM $back_pid1;
kill -SIGTERM $back_pid2;
kill -SIGTERM $back_pid3;
kill -SIGTERM $back_pid4;
kill -SIGTERM $back_pid5;
wait >/dev/null 2>&1;

#Test that volume heal info reports files even when self-heal
#options are disabled
TEST touch $M0/f
TEST mkdir $M0/d
#DATA
TEST $CLI volume set $V0 cluster.data-self-heal off
EXPECT "off" volume_option $V0 cluster.data-self-heal
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}4
echo abc > $M0/f
EXPECT 1 afr_get_pending_heal_count $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "Y" glustershd_up_status
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 4
TEST $CLI volume heal $V0
EXPECT_WITHIN 30 "0" afr_get_pending_heal_count $V0
TEST $CLI volume set $V0 cluster.data-self-heal on

#METADATA
TEST $CLI volume set $V0 cluster.metadata-self-heal off
EXPECT "off" volume_option $V0 cluster.metadata-self-heal
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST chmod 777 $M0/f
EXPECT 1 afr_get_pending_heal_count $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "Y" glustershd_up_status
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 4
TEST $CLI volume heal $V0
EXPECT_WITHIN 30 "0" afr_get_pending_heal_count $V0
TEST $CLI volume set $V0 cluster.metadata-self-heal on

#ENTRY
TEST $CLI volume set $V0 cluster.entry-self-heal off
EXPECT "off" volume_option $V0 cluster.entry-self-heal
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST touch $M0/d/a
EXPECT 2 afr_get_pending_heal_count $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "Y" glustershd_up_status
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 4
TEST $CLI volume heal $V0
EXPECT_WITHIN 30 "0" afr_get_pending_heal_count $V0
TEST $CLI volume set $V0 cluster.entry-self-heal on

#Negative test cases
#Fail volume does not exist case
TEST ! $CLI volume heal fail info

#Fail volume stopped case
TEST $CLI volume stop $V0
TEST ! $CLI volume heal $V0 info

#Fail non-replicate volume info
TEST $CLI volume delete $V0
TEST $CLI volume create $V0 $H0:$B0/${V0}{6}
TEST $CLI volume start $V0
TEST ! $CLI volume heal $V0 info

cleanup
