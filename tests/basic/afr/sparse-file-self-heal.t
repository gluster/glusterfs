#!/bin/bash

#This file checks if self-heal of files with holes is working properly or not
#bigger is 2M, big is 1M, small is anything less
#Also tests if non-sparse files with zeroes in it are healed correctly w.r.t
#disk usage.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 data-self-heal-algorithm full
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
TEST dd if=/dev/urandom of=$M0/small count=1 bs=1024k
TEST dd if=/dev/urandom of=$M0/bigger2big count=1 bs=2048k
TEST dd if=/dev/urandom of=$M0/big2bigger count=1 bs=1024k
TEST truncate -s 1G $M0/FILE

TEST kill_brick $V0 $H0 $B0/${V0}0

#File with >128k size hole
TEST truncate -s 1M $M0/big
big_md5sum=$(md5sum $M0/big | awk '{print $1}')

#File with <128k hole
TEST truncate -s 0 $M0/small
TEST truncate -s 64k $M0/small
small_md5sum=$(md5sum $M0/small | awk '{print $1}')

#Bigger file truncated to big size hole.
TEST truncate -s 0 $M0/bigger2big
TEST truncate -s 1M $M0/bigger2big
bigger2big_md5sum=$(md5sum $M0/bigger2big | awk '{print $1}')

#Big file truncated to Bigger size hole
TEST truncate -s 2M $M0/big2bigger
big2bigger_md5sum=$(md5sum $M0/big2bigger | awk '{print $1}')

#Write data to file and restore its sparseness
TEST dd if=/dev/urandom of=$M0/FILE count=1 bs=131072
TEST truncate -s 1G $M0/FILE

#Create a non-sparse file containing zeroes.
TEST dd if=/dev/zero of=$M0/zeroedfile bs=1024 count=1024

$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST gluster volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#If the file system of bricks is XFS and speculative preallocation is on,
#dropping cahce should be done to free speculatively pre-allocated blocks
#by XFS.
drop_cache $M0

big_md5sum_0=$(md5sum $B0/${V0}0/big | awk '{print $1}')
small_md5sum_0=$(md5sum $B0/${V0}0/small | awk '{print $1}')
bigger2big_md5sum_0=$(md5sum $B0/${V0}0/bigger2big | awk '{print $1}')
big2bigger_md5sum_0=$(md5sum $B0/${V0}0/big2bigger | awk '{print $1}')

EXPECT $big_md5sum echo $big_md5sum_0
EXPECT $small_md5sum echo $small_md5sum_0
EXPECT $big2bigger_md5sum echo $big2bigger_md5sum_0
EXPECT $bigger2big_md5sum echo $bigger2big_md5sum_0


EXPECT "1" has_holes $B0/${V0}0/big
#Because self-heal writes the final chunk hole should not be there for
#files < 128K
EXPECT "0" has_holes $B0/${V0}0/small
# Since source is smaller than sink, self-heal does blind copy so no holes will
# be present
EXPECT "0" has_holes $B0/${V0}0/bigger2big
EXPECT "1" has_holes $B0/${V0}0/big2bigger

#Check that self-heal has not written 0s to sink and made it non-sparse.
USED_KB=`du -s $B0/${V0}0/FILE|cut -f1`
TEST [ $USED_KB -lt 1000000 ]

#Check that the non-sparse file has the same file size on both bricks and that
#the disk usage is greater than or equal to the file size. We could have checked
#that the disk usage is just equal to the file size but XFS does speculative
#preallocation due to which disk usage can be more than the file size.
STAT_SIZE1=$(stat -c "%s" $B0/${V0}0/zeroedfile)
STAT_SIZE2=$(stat -c "%s" $B0/${V0}1/zeroedfile)
TEST [ $STAT_SIZE1 -eq $STAT_SIZE2 ]
USED_KB1="$((`stat -c  %b $B0/${V0}0/zeroedfile` * `stat -c %B $B0/${V0}0/zeroedfile`))"
TEST [ $USED_KB1 -ge $STAT_SIZE1 ]
USED_KB2="$((`stat -c  %b $B0/${V0}1/zeroedfile` * `stat -c %B $B0/${V0}1/zeroedfile`))"
TEST [ $USED_KB2 -ge $STAT_SIZE2 ]

TEST rm -f $M0/*

#check the same tests with diff self-heal
TEST $CLI volume set $V0 data-self-heal-algorithm diff

TEST dd if=/dev/urandom of=$M0/small count=1 bs=1024k
TEST dd if=/dev/urandom of=$M0/big2bigger count=1 bs=1024k
TEST dd if=/dev/urandom of=$M0/bigger2big count=1 bs=2048k
TEST truncate -s 1G $M0/FILE

TEST kill_brick $V0 $H0 $B0/${V0}0

#File with >128k size hole
TEST truncate -s 1M $M0/big
big_md5sum=$(md5sum $M0/big | awk '{print $1}')

#File with <128k hole
TEST truncate -s 0 $M0/small
TEST truncate -s 64k $M0/small
small_md5sum=$(md5sum $M0/small | awk '{print $1}')

#Bigger file truncated to big size hole
TEST truncate -s 0 $M0/bigger2big
TEST truncate -s 1M $M0/bigger2big
bigger2big_md5sum=$(md5sum $M0/bigger2big | awk '{print $1}')

#Big file truncated to Bigger size hole
TEST truncate -s 2M $M0/big2bigger
big2bigger_md5sum=$(md5sum $M0/big2bigger | awk '{print $1}')

#Write data to file and restore its sparseness
TEST dd if=/dev/urandom of=$M0/FILE count=1 bs=131072
TEST truncate -s 1G $M0/FILE

#Create a non-sparse file containing zeroes.
TEST dd if=/dev/zero of=$M0/zeroedfile bs=1024 count=1024

$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST gluster volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#If the file system of bricks is XFS and speculative preallocation is on,
#dropping cahce should be done to free speculatively pre-allocated blocks
#by XFS.
drop_cache $M0

big_md5sum_0=$(md5sum $B0/${V0}0/big | awk '{print $1}')
small_md5sum_0=$(md5sum $B0/${V0}0/small | awk '{print $1}')
bigger2big_md5sum_0=$(md5sum $B0/${V0}0/bigger2big | awk '{print $1}')
big2bigger_md5sum_0=$(md5sum $B0/${V0}0/big2bigger | awk '{print $1}')

EXPECT $big_md5sum echo $big_md5sum_0
EXPECT $small_md5sum echo $small_md5sum_0
EXPECT $big2bigger_md5sum echo $big2bigger_md5sum_0
EXPECT $bigger2big_md5sum echo $bigger2big_md5sum_0

EXPECT "1" has_holes $B0/${V0}0/big
EXPECT "1" has_holes $B0/${V0}0/big2bigger
EXPECT "0" has_holes $B0/${V0}0/bigger2big
EXPECT "0" has_holes $B0/${V0}0/small

#Check that self-heal has not written 0s to sink and made it non-sparse.
USED_KB=`du -s $B0/${V0}0/FILE|cut -f1`
TEST [ $USED_KB -lt 1000000 ]

#Check that the non-sparse file has the same file size on both bricks and that
#the disk usage is greater than or equal to the file size. We could have checked
#that the disk usage is just equal to the file size but XFS does speculative
#preallocation due to which disk usage can be more than the file size.
STAT_SIZE1=$(stat -c "%s" $B0/${V0}0/zeroedfile)
STAT_SIZE2=$(stat -c "%s" $B0/${V0}1/zeroedfile)
TEST [ $STAT_SIZE1 -eq $STAT_SIZE2 ]
USED_KB1="$((`stat -c  %b $B0/${V0}0/zeroedfile` * `stat -c %B $B0/${V0}0/zeroedfile`))"
TEST [ $USED_KB1 -ge $STAT_SIZE1 ]
USED_KB2="$((`stat -c  %b $B0/${V0}1/zeroedfile` * `stat -c %B $B0/${V0}1/zeroedfile`))"
TEST [ $USED_KB2 -ge $STAT_SIZE2 ]

cleanup
