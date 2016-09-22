#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../afr.rc

cleanup

TESTS_EXPECTED_IN_LOOP=12

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume heal $V0 granular-entry-heal enable

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

# Create files under root
for i in {1..4}
do
        echo $i > $M0/f$i
done

# Create a directory and few files under it
TEST mkdir $M0/dir
gfid_dir=$(get_gfid_string $M0/dir)

for i in {1..3}
do
        echo $i > $M0/dir/f$i
done

# Kill brick-0.
TEST kill_brick $V0 $H0 $B0/${V0}0

# Create more files
for i in {5..6}
do
        echo $i > $M0/f$i
done

# Test that the index associated with '/' is created on B1.
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID

# Check for successful creation of granular entry indices
for i in {5..6}
do
        TEST_IN_LOOP stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f$i
done

# Delete an existing file
TEST unlink $M0/f1

# Rename an existing file
TEST mv $M0/f2 $M0/f2_renamed

# Create a hard link on f3
TEST ln $M0/f3 $M0/link

# Create a symlink on f4
TEST ln -s $M0/f4 $M0/symlink

# Check for successful creation of granular entry indices
for i in {1..2}
do
        TEST_IN_LOOP stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f$i
done

TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f2_renamed
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/link
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/symlink

# Create a file and also delete it. This is to test deletion of stale indices during heal.
TEST touch $M0/file_stale
TEST unlink $M0/file_stale
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/file_stale

# Create a directory and create its subdirs and files while a brick is down
TEST mkdir -p $M0/newdir/newsubdir

for i in {1..3}
do
        echo $i > $M0/newdir/f$i
        echo $i > $M0/newdir/newsubdir/f$i
done

TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/newdir
gfid_newdir=$(get_gfid_string $M0/newdir)
gfid_newsubdir=$(get_gfid_string $M0/newdir/newsubdir)
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newdir/newsubdir
# Check if 'data' segment of the changelog is set for the newly created directories 'newdir' and 'newsubdir'
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}1/newdir trusted.afr.$V0-client-0 data
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}1/newdir/newsubdir trusted.afr.$V0-client-0 data

# Test that removal of an entire sub-tree in the hierarchy works.
TEST rm -rf $M0/dir

TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/dir
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/f1
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/f2
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/f3

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST gluster volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Test if data was healed
for i in {5..6}
do
        TEST_IN_LOOP diff $B0/${V0}0/f$i $B0/${V0}1/f$i
done

for i in {1..3}
do
        TEST_IN_LOOP diff $B0/${V0}0/newdir/f$i $B0/${V0}1/newdir/f$i
        TEST_IN_LOOP diff $B0/${V0}0/newdir/newsubdir/f$i $B0/${V0}1/newdir/newsubdir/f$i
done

# Verify that all the deleted names have been removed on the sink brick too by self-heal.
TEST ! stat $B0/${V0}0/f1
TEST ! stat $B0/${V0}0/f2
TEST   stat $B0/${V0}0/f2_renamed
TEST   stat $B0/${V0}0/symlink
EXPECT "3" get_hard_link_count $B0/${V0}0/f3
EXPECT "f4" readlink $B0/${V0}0/symlink
TEST ! stat $B0/${V0}0/dir

# Now verify that there are no name indices left after self-heal
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/f2_renamed
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/link
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/symlink
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/dir
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/newdir
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID/file_stale

TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir/f3

TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newdir/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newdir/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newdir/f3
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newsubdir/f1
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newsubdir/f2
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newsubdir/f3

TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$ROOT_GFID
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newdir
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_newsubdir
TEST ! stat $B0/${V0}1/.glusterfs/indices/entry-changes/$gfid_dir

cleanup
