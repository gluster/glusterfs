#!/bin/bash
#Self-heal tests
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 write-behind off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

cd $M0
TEST `echo "line1" >> file1`
TEST mkdir dir1
TEST mkdir dir2
TEST mkdir -p dir1/dira/dirb
TEST `echo "line1">>dir1/dira/dirb/file1`
TEST mkdir delete_me
TEST `echo "line1" >> delete_me/file1`

#brick0 has witnessed the second write while brick1 is down.
TEST kill_brick $V0 $H0 $B0/brick1
TEST `echo "line2" >> file1`
TEST `echo "line2" >> dir1/dira/dirb/file1`
TEST `echo "line2" >> delete_me/file1`

#Toggle the bricks that are up/down.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/brick0

#Rename when the 'source' brick0 for data-selfheals is down.
mv file1 file2
mv dir1/dira dir2

#Delete a dir when brick0 is down.
rm -rf delete_me
cd -

#Bring everything up and trigger heal
TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/brick0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/brick1

#Remount to avoid reading from caches
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
EXPECT "line2" tail -1 $M0/file2
EXPECT "line2" tail -1 $M0/dir2/dira/dirb/file1
TEST ! stat $M0/delete_me/file1
TEST ! stat $M0/delete_me

anon_inode_name=$(ls -a $B0/brick0 | grep glusterfs-anonymous-inode)
TEST [[ -d $B0/brick0/$anon_inode_name ]]
TEST [[ -d $B0/brick1/$anon_inode_name ]]
cleanup
