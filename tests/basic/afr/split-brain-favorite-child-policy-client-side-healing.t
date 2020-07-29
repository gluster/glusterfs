#!/bin/bash

#Test the client side split-brain resolution
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

GET_MDATA_PATH=$(dirname $0)/../../utils
build_tester $GET_MDATA_PATH/get-mdata-xattr.c

TEST glusterd
TEST pidof glusterd

count_files () {
         ls $1 | wc -l
}

#Create replica 2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 1
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on

TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST mkdir $M0/data
TEST touch $M0/data/file


############ Client side healing using favorite-child-policy = mtime #################
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/data/file bs=1024 count=1024
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/data/file bs=1024 count=1024

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

mtime1=$(get_mtime $B0/${V0}0/data/file)
mtime2=$(get_mtime $B0/${V0}1/data/file)
if (( $(echo "$mtime1 > $mtime2" | bc -l) )); then
        LATEST_MTIME_MD5=$(md5sum $B0/${V0}0/data/file | cut -d\  -f1)
else
        LATEST_MTIME_MD5=$(md5sum $B0/${V0}1/data/file | cut -d\  -f1)
fi

#file will be in split-brain
cat $M0/data/file > /dev/null
EXPECT "1" echo $?

TEST $CLI volume set $V0 cluster.favorite-child-policy mtime
TEST $CLI volume start $V0 force

EXPECT_WITHIN $HEAL_TIMEOUT "^2$" afr_get_split_brain_count $V0
cat $M0/data/file > /dev/null
EXPECT "0" echo $?
M0_MD5=$(md5sum $M0/data/file | cut -d\  -f1)
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_get_split_brain_count $V0
TEST [ "$LATEST_MTIME_MD5" == "$M0_MD5" ]

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

B0_MD5=$(md5sum $B0/${V0}0/data/file | cut -d\  -f1)
B1_MD5=$(md5sum $B0/${V0}1/data/file | cut -d\  -f1)
TEST [ "$LATEST_MTIME_MD5" == "$B0_MD5" ]
TEST [ "$LATEST_MTIME_MD5" == "$B1_MD5" ]

############ Client side directory conservative merge #################
TEST $CLI volume reset $V0 cluster.favorite-child-policy
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/data/test
files=$(count_files $M0/data)
EXPECT "2" echo $files
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST touch $M0/data/test1
files=$(count_files $M0/data)
EXPECT "2" echo $files

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

#data dir will be in entry split-brain
ls $M0/data > /dev/null
EXPECT "2" echo $?

TEST $CLI volume set $V0 cluster.favorite-child-policy mtime

EXPECT_WITHIN $HEAL_TIMEOUT "^2$" afr_get_split_brain_count $V0


ls $M0/data > /dev/null
EXPECT "0" echo $?

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_get_split_brain_count $V0
#Entry Split-brain is gone, but data self-heal is pending on the files
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0

cat $M0/data/test > /dev/null
cat $M0/data/test1 > /dev/null

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
files=$(count_files $M0/data)
EXPECT "3" echo $files

TEST force_umount $M0
TEST rm $GET_MDATA_PATH/get-mdata-xattr

cleanup
