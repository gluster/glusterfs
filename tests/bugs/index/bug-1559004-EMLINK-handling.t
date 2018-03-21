#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup
TESTS_EXPECTED_IN_LOOP=30
SCRIPT_TIMEOUT=1000

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST touch $B0/ext4-1
TEST touch $B0/ext4-2
TEST touch $B0/ext4-3
TEST truncate -s 2GB $B0/ext4-1
TEST truncate -s 2GB $B0/ext4-2
TEST truncate -s 2GB $B0/ext4-3

TEST mkfs.ext4 -F $B0/ext4-1
TEST mkfs.ext4 -F $B0/ext4-2
TEST mkfs.ext4 -F $B0/ext4-3
TEST mkdir $B0/ext41
TEST mkdir $B0/ext42
TEST mkdir $B0/ext43
TEST mount -t ext4 -o loop $B0/ext4-1 $B0/ext41
TEST mount -t ext4 -o loop $B0/ext4-2 $B0/ext42
TEST mount -t ext4 -o loop $B0/ext4-3 $B0/ext43

TEST $CLI volume create $V0 replica 3 $H0:$B0/ext4{1,2,3}
TEST $CLI volume start $V0
TEST $CLI volume heal $V0 granular-entry-heal enable
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST kill_brick $V0 $H0 $B0/ext41
for i in {1..15}
do
        TEST_IN_LOOP mkdir $M0/d${i}
        TEST_IN_LOOP touch $M0/d${i}/{1..5000}
done

#On ext4 max number of hardlinks is ~65k, so there should be 2 base index files
EXPECT "^2$" echo $(ls $B0/ext42/.glusterfs/indices/xattrop | grep xattrop | wc -l)
EXPECT "^2$" echo $(ls $B0/ext42/.glusterfs/indices/entry-changes | grep entry-changes | wc -l)

#Number of hardlinks: 75000 for files, 15 for dirs and 2 for base-indices
#and root-dir for xattrop
EXPECT "75018" echo $(ls -l $B0/ext42/.glusterfs/indices/xattrop | grep xattrop | awk '{sum+=$2} END{print sum}')
EXPECT "75017" echo $(ls -l $B0/ext42/.glusterfs/indices/entry-changes | grep entry-changes | awk '{sum+=$2} END{print sum}')

cleanup
