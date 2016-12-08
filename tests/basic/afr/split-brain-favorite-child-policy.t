#!/bin/bash

#Test the split-brain resolution CLI commands.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

#Create replica 2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/file

############ Healing using favorite-child-policy = ctime #################
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

#file fill in split-brain
cat $M0/file > /dev/null
EXPECT "1" echo $?

# Umount to prevent further FOPS on the file, then find the brick with latest ctime.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
ctime1=`stat -c "%.Z"  $B0/${V0}0/file`
ctime2=`stat -c "%.Z"  $B0/${V0}1/file`
if (( $(echo "$ctime1 > $ctime2" | bc -l) )); then
        LATEST_CTIME_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
else
        LATEST_CTIME_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
fi
TEST $CLI volume set $V0 cluster.favorite-child-policy ctime
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
B0_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
B1_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
TEST [ "$LATEST_CTIME_MD5" == "$B0_MD5" ]
TEST [ "$LATEST_CTIME_MD5" == "$B1_MD5" ]
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cat $M0/file > /dev/null
EXPECT "0" echo $?

############ Healing using favorite-child-policy = mtime #################
TEST $CLI volume set $V0 cluster.favorite-child-policy none
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

#file still in split-brain
cat $M0/file > /dev/null
EXPECT "1" echo $?

#We know that the second brick has latest mtime.
LATEST_CTIME_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
TEST $CLI volume set $V0 cluster.favorite-child-policy mtime
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
cat $M0/file > /dev/null
EXPECT "0" echo $?
HEALED_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
TEST [ "$LATEST_CTIME_MD5" == "$HEALED_MD5" ]

############ Healing using favorite-child-policy = size #################
TEST $CLI volume set $V0 cluster.favorite-child-policy none
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=10240

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

#file fill in split-brain
cat $M0/file > /dev/null
EXPECT "1" echo $?

#We know that the second brick has the bigger size file.
BIGGER_FILE_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
TEST $CLI volume set $V0 cluster.favorite-child-policy size
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
cat $M0/file > /dev/null
EXPECT "0" echo $?
HEALED_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
TEST [ "$BIGGER_FILE_MD5" == "$HEALED_MD5" ]

############ Healing using favorite-child-policy = majority on replica-3  #################

#Convert volume to replica-3
TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST $CLI volume set $V0 cluster.quorum-type none
TEST $CLI volume set $V0 cluster.favorite-child-policy none
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=10240

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0

#file fill in split-brain
cat $M0/file > /dev/null
EXPECT "1" echo $?

#We know that the second and third bricks agree with each other. Pick any one of them.
MAJORITY_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
TEST $CLI volume set $V0 cluster.favorite-child-policy majority
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
cat $M0/file > /dev/null
EXPECT "0" echo $?
HEALED_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
TEST [ "$MAJORITY_MD5" == "$HEALED_MD5" ]

TEST force_umount $M0
cleanup
