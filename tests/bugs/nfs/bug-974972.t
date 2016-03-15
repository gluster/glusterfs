#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

#This script checks that nfs mount does not fail lookup on files with split-brain
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0
TEST touch $N0/1
TEST kill_brick ${V0} ${H0} ${B0}/${V0}1
echo abc > $N0/1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" nfs_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_nfs $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_nfs $V0 1

TEST kill_brick ${V0} ${H0} ${B0}/${V0}0
echo def > $N0/1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" nfs_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_nfs $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_nfs $V0 1

#Lookup should not fail
TEST ls $N0/1
TEST ! cat $N0/1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
cleanup
