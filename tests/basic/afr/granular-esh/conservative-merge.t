#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../afr.rc

cleanup

TESTS_EXPECTED_IN_LOOP=4

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume heal $V0 granular-entry-heal enable

TEST $GFS --volfile-id=$V0 -s $H0 $M0

TEST mkdir $M0/dir
gfid_dir=$(get_gfid_string $M0/dir)

echo "1" > $M0/f1
echo "2" > $M0/f2

TEST kill_brick $V0 $H0 $B0/${V0}0

TEST mkdir $M0/dir2
gfid_dir2=$(get_gfid_string $M0/dir2)
TEST unlink $M0/f1
echo "3" > $M0/f3
TEST mkdir $M0/dir/subdir
gfid_subdir=$(get_gfid_string $M0/dir/subdir)
echo "dir2-1" > $M0/dir2/f1
echo "subdir-1" > $M0/dir/subdir/f1

TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/dir2
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f1
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f3
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir2
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir2/f1
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/subdir
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_subdir
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_subdir/f1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}1

TEST mkdir $M0/dir3
gfid_dir3=$(get_gfid_string $M0/dir3)
# Root is now in split-brain.
TEST mkdir $M0/dir/subdir2
gfid_subdir2=$(get_gfid_string $M0/dir/subdir2)
# /dir is now in split-brain.
echo "4" > $M0/f4
TEST unlink $M0/f2
echo "dir3-1" > $M0/dir3/f1
echo "subdir2-1" > $M0/dir/subdir2/f1

TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID/dir3
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID/f2
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID/f4
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_dir
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_dir/subdir2
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_dir3
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_dir3/f1
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_subdir2
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_subdir2/f1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Make sure entry self-heal did the right thing in terms of impunging deleted
# files in the event of a split-brain.
TEST stat $M0/f1
TEST stat $M0/f2

# Test if data was healed
for i in {1..4}
do
        TEST_IN_LOOP diff $B0/${V0}0/f$i $B0/${V0}1/f$i
done

TEST diff $B0/${V0}0/dir2/f1 $B0/${V0}1/dir2/f1
EXPECT "dir2-1" cat $M0/dir2/f1

TEST diff $B0/${V0}0/dir/subdir/f1 $B0/${V0}1/dir/subdir/f1
EXPECT "subdir-1" cat $M0/dir/subdir/f1

TEST diff $B0/${V0}0/dir3/f1 $B0/${V0}1/dir3/f1
EXPECT "dir3-1" cat $M0/dir3/f1

TEST diff $B0/${V0}0/dir/subdir2/f1 $B0/${V0}1/dir/subdir2/f1
EXPECT "subdir2-1" cat $M0/dir/subdir2/f1

# Verify that all name indices have been removed after a successful heal.
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/dir2
TEST ! stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID/dir3
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f1
TEST ! stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f3
TEST ! stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID/f4
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir2/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/subdir
TEST ! stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_dir/subdir2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_subdir/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir3/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_subdir2/f1

TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID
TEST ! stat $B0/${V0}0/.glusterfs/indices/entry-changes/$ROOT_GFID
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir
TEST ! stat $B0/${V0}0/.glusterfs/indices/entry-changes/$gfid_dir
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_subdir
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir3
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_subdir2

cleanup
