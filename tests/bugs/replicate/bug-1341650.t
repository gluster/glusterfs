#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..2}
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
TEST $CLI volume quota $V0 enable
TEST $CLI volume set $V0 quota-deem-statfs on
TEST $CLI volume set $V0 soft-timeout 0
TEST $CLI volume set $V0 hard-timeout 0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0
TEST mkdir $M0/dir
TEST $CLI volume quota $V0 limit-objects /dir 10

TEST touch $M0/dir/file1
TEST touch $M0/dir/file2
TEST touch $M0/dir/file3
TEST touch $M0/dir/file4
TEST touch $M0/dir/file5

# Kill 3rd brick and create entries
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST touch $M0/dir/file6
TEST touch $M0/dir/file7
TEST touch $M0/dir/file8
TEST touch $M0/dir/file9

# Quota object limit is reached. Remove object for create to succeed.
TEST ! touch $M0/dir/file10

TEST rm $M0/dir/file1
TEST touch $M0/dir/file10

EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.dirty $B0/${V0}0/dir
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.dirty $B0/${V0}1/dir

TEST $CLI volume start $V0 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#Check that no convervative merge happened.file1 must not be present on any brick.
TEST ! stat $B0/${V0}0/dir/file1
TEST ! stat $B0/${V0}1/dir/file1
TEST ! stat $B0/${V0}2/dir/file1

TEST umount $M0
cleanup
