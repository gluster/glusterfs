#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

function check_gfid_and_link_count
{
        local file=$1

        file_gfid_b0=$(gf_get_gfid_xattr $B0/${V0}0/$file)
        TEST [ ! -z $file_gfid_b0 ]
        file_gfid_b1=$(gf_get_gfid_xattr $B0/${V0}1/$file)
        file_gfid_b2=$(gf_get_gfid_xattr $B0/${V0}2/$file)
        EXPECT $file_gfid_b0 echo $file_gfid_b1
        EXPECT $file_gfid_b0 echo $file_gfid_b2

        EXPECT "2" stat -c %h $B0/${V0}0/$file
        EXPECT "2" stat -c %h $B0/${V0}1/$file
        EXPECT "2" stat -c %h $B0/${V0}2/$file
}
TESTS_EXPECTED_IN_LOOP=30

##############################################################################
# Test on 1x3 volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume start $V0;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2


# Create files directly in the backend on different bricks
echo $RANDOM >> $B0/${V0}0/file1
echo $RANDOM >> $B0/${V0}1/file2
echo $RANDOM >> $B0/${V0}2/file3

# To prevent is_fresh_file code path
sleep 2

# Access them from mount to trigger name + gfid heal.
TEST stat $M0/file1
TEST stat $M0/file2
TEST stat $M0/file3

# Launch index heal to complete any pending data/metadata heals.
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Check each file has a gfid and the .glusterfs hardlink
check_gfid_and_link_count file1
check_gfid_and_link_count file2
check_gfid_and_link_count file3

TEST rm $M0/file1
TEST rm $M0/file2
TEST rm $M0/file3
cleanup;

##############################################################################
# Test on 1x (2+1) volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume start $V0;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2


# Create files directly in the backend on different bricks
echo $RANDOM >> $B0/${V0}0/file1
echo $RANDOM >> $B0/${V0}1/file2
touch $B0/${V0}2/file3

# To prevent is_fresh_file code path
sleep 2

# Access them from mount to trigger name + gfid heal.
TEST stat $M0/file1
TEST stat $M0/file2

# Though file is created on all 3 bricks, lookup will fail as arbiter blames the
# other 2 bricks and ariter is not 'readable'.
TEST ! stat $M0/file3

# Launch index heal to complete any pending data/metadata heals.
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Now file3 should be accesible from mount.
TEST stat $M0/file3

# Check each file has a gfid and the .glusterfs hardlink
check_gfid_and_link_count file1
check_gfid_and_link_count file2
check_gfid_and_link_count file3

TEST rm $M0/file1
TEST rm $M0/file2
TEST rm $M0/file3
cleanup;
