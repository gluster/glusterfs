#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

#This test checks that partial failure of fop results in main fop failure only
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume create $V1 $H0:$B0/${V1}{0..5}
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=/$V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST dd if=/dev/urandom of=$M0/a bs=12347 count=1
TEST dd if=/dev/urandom of=$M0/b bs=12347 count=1
TEST cp $M0/b $M0/c
TEST fallocate -p -l 101 $M0/c
TEST $CLI volume stop $V0
TEST $CLI volume set $V0 debug.delay-gen posix;
TEST $CLI volume set $V0 delay-gen.delay-duration 10000000;
TEST $CLI volume set $V0 delay-gen.enable WRITE;
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 disperse.quorum-count 6
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
cksum=$(dd if=$M0/a bs=12345 count=1 | md5sum | awk '{print $1}')
truncate -s 12345 $M0/a & #While write is waiting for 5 seconds, introduce failure
fallocate -p -l 101 $M0/b &
sleep 1
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST wait
TEST $CLI volume start $V0 force
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count ${V0}
EXPECT "12345" stat --format=%s $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0
cksum_after_heal=$(dd if=$M0/a | md5sum | awk '{print $1}')
TEST [[ $cksum == $cksum_after_heal ]]
cksum=$(dd if=$M0/c | md5sum | awk '{print $1}')
cksum_after_heal=$(dd if=$M0/b | md5sum | awk '{print $1}')
TEST [[ $cksum == $cksum_after_heal ]]

cleanup;
