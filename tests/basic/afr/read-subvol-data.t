#!/bin/bash
#Test if the source is selected based on data transaction for a regular file.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

#Init
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#Test
TEST $CLI volume set $V0 cluster.read-subvolume $V0-client-1
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST dd if=/dev/urandom of=$M0/afr_success_5.txt bs=1024k count=1
TEST kill_brick $V0 $H0 $B0/brick0
TEST dd if=/dev/urandom of=$M0/afr_success_5.txt bs=1024k count=10
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "10485760" echo `ls -l $M0/afr_success_5.txt | awk '{ print $5}'`

#Cleanup
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST rm -rf $B0/*
