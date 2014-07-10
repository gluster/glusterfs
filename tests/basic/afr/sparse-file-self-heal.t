#!/bin/bash

#This file checks if self-heal of files with holes is working properly or not
#bigger is 2M, big is 1M, small is anything less
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 data-self-heal-algorithm full
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST dd if=/dev/urandom of=$M0/small count=1 bs=1024k
TEST dd if=/dev/urandom of=$M0/bigger2big count=1 bs=2048k
TEST dd if=/dev/urandom of=$M0/big2bigger count=1 bs=1024k

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

$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST gluster volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "0" afr_get_pending_heal_count $V0

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

TEST rm -f $M0/*

#check the same tests with diff self-heal
TEST $CLI volume set $V0 data-self-heal-algorithm diff

TEST dd if=/dev/urandom of=$M0/small count=1 bs=1024k
TEST dd if=/dev/urandom of=$M0/big2bigger count=1 bs=1024k
TEST dd if=/dev/urandom of=$M0/bigger2big count=1 bs=2048k

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

$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST gluster volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "0" afr_get_pending_heal_count $V0

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

cleanup
