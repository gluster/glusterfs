#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

ec_test_dir=$M0/test

function ec_test_generate_src()
{
    mkdir -p $ec_test_dir
    for i in `seq 0 19`; do
        dd if=/dev/zero of=$ec_test_dir/$i.c bs=1024 count=2
    done
}

function ec_test_make()
{
    for i in `ls *.c`; do
        file=`basename $i`
        filename=${file%.*}
        cp $i $filename.o
    done
}

## step 1
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 7 redundancy 3 $H0:$B0/${V0}{0..6}
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "7" ec_child_up_count $V0 0

## step 2
TEST ec_test_generate_src

cd $ec_test_dir
TEST ec_test_make

## step 3
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT '5' online_brick_count

TEST rm -f *.o
TEST ec_test_make

## step 4
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "7" online_brick_count

# active heal
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST rm -f *.o
TEST ec_test_make

## step 5
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT '5' online_brick_count

TEST rm -f *.o
TEST ec_test_make

EXPECT '5' online_brick_count

## step 6
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "7" online_brick_count

# self-healing
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST rm -f *.o
TEST ec_test_make

TEST pidof glusterd
EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT '7' online_brick_count

## cleanup
cd
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST rm -rf $B0/*

cleanup;
