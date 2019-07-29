#!/bin/bash
#
# This will test self healing of ctime xattr 'trusted.glusterfs.mdata'
#
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup

#cleate and start volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3}
TEST $CLI volume start $V0

#Mount the volume
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# Create files
mkdir $M0/dir1
echo "Initial content" > $M0/file1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/file1

# Kill brick
TEST kill_brick $V0 $H0 $B0/${V0}3

echo "B3 is down" >> $M0/file1
echo "Change dir1 time attributes" > $M0/dir1/dir1_file1
echo "Entry heal file" > $M0/entry_heal_file1
mkdir $M0/entry_heal_dir1

# Check xattr
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '2' get_mdata_uniq_count $B0/${V0}{1..3}/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '2' get_mdata_uniq_count $B0/${V0}{1..3}/file1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT '2' get_mdata_count $B0/${V0}{1..3}/dir1/dir1_file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/dir1/dir1_file1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT '2' get_mdata_count $B0/${V0}{1..3}/entry_heal_file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/entry_heal_file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '2' get_mdata_count $B0/${V0}{1..3}/entry_heal_dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/entry_heal_dir1

TEST $CLI volume start $V0 force
$CLI volume heal $V0

# Check xattr
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/file1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/dir1/dir1_file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/dir1/dir1_file1

EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/entry_heal_file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/entry_heal_file1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '3' get_mdata_count $B0/${V0}{1..3}/entry_heal_dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '1' get_mdata_uniq_count $B0/${V0}{1..3}/entry_heal_dir1

cleanup;
