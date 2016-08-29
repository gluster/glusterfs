#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 4 redundancy 1 $H0:$B0/${V0}{0..3}
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

TEST dd if=/dev/urandom of=$M0/test1 bs=1024k count=2
cs=$(sha1sum $M0/test1 | awk '{ print $1 }')

TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT '3' online_brick_count

TEST cp $M0/test1 $M0/test2
EXPECT "$cs" echo $(sha1sum $M0/test2 | awk '{ print $1 }')

TEST $CLI volume start $V0 force
EXPECT '4' online_brick_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT "699392" stat -c "%s" $B0/${V0}0/test2

# force cache clear
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT '3' online_brick_count

EXPECT "$cs" echo $(sha1sum $M0/test2 | awk '{ print $1 }')

## cleanup
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
