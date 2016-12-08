#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off

TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
echo "some data" > $M0/datafile
EXPECT 0 echo $?
TEST touch $M0/mdatafile
TEST mkdir $M0/dir

#Kill a brick and perform I/O to have pending heals.
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" afr_child_up_status $V0 0

#pending data heal
echo "some more data" >> $M0/datafile
EXPECT 0 echo $?

#pending metadata heal
TEST chmod +x $M0/mdatafile

#pending entry heal. Also causes pending metadata/data heals on file{1..5}
TEST touch $M0/dir/file{1..5}

EXPECT 8 get_pending_heal_count $V0

#After brick comes back up, access from client should not trigger heals
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

#Medatada heal via explicit lookup must not happen
TEST ls $M0/mdatafile

#Inode refresh must not trigger data and entry heals.
#To trigger inode refresh for sure, the volume is unmounted and mounted each time.
#Check that data heal does not happen.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST cat $M0/datafile
#Check that entry heal does not happen.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST ls $M0/dir

#No heal must have happened
EXPECT 8 get_pending_heal_count $V0

#Enable heal client side heal options and trigger heals
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on

#Metadata heal is triggered by lookup without need for inode refresh.
TEST ls $M0/mdatafile
EXPECT 7 get_pending_heal_count $V0

#Inode refresh must trigger data and entry heals.
#To trigger inode refresh for sure, the volume is unmounted and mounted each time.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST cat $M0/datafile
EXPECT_WITHIN $HEAL_TIMEOUT 6 get_pending_heal_count $V0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST ls $M0/dir
EXPECT 5 get_pending_heal_count $V0

TEST cat  $M0/dir/file1
TEST cat  $M0/dir/file2
TEST cat  $M0/dir/file3
TEST cat  $M0/dir/file4
TEST cat  $M0/dir/file5

EXPECT 0 get_pending_heal_count $V0
cleanup;
