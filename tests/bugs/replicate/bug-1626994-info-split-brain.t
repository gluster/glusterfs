#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

# Test to check dirs having dirty xattr do not show up in info split-brain.

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

# Create base entry in indices/xattrop
echo "Data" > $M0/FILE
rm -f $M0/FILE
EXPECT "1" count_index_entries $B0/${V0}0
EXPECT "1" count_index_entries $B0/${V0}1
EXPECT "1" count_index_entries $B0/${V0}2

TEST mkdir $M0/dirty_dir
TEST mkdir $M0/pending_dir

# Set dirty xattrs on all bricks to simulate the case where entry transaction
# succeeded only the pre-op phase.
TEST setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}0/dirty_dir
TEST setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}1/dirty_dir
TEST setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}2/dirty_dir
create_brick_xattrop_entry $B0/${V0}0  dirty_dir
# Should not show up as split-brain.
EXPECT "0" afr_get_split_brain_count $V0

# replace/reset brick case where the new brick has dirty and the other 2 bricks
# blame it should not be reported as split-brain.
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000000000001 $B0/${V0}0
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000000000001 $B0/${V0}1
TEST setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}2
create_brick_xattrop_entry $B0/${V0}0 "/"
# Should not show up as split-brain.
EXPECT "0" afr_get_split_brain_count $V0

# Set pending xattrs on all bricks blaming each other to simulate the case of
# entry split-brain.
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}0/pending_dir
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000000000001 $B0/${V0}1/pending_dir
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/${V0}2/pending_dir
create_brick_xattrop_entry $B0/${V0}0 pending_dir
# Should show up as split-brain.
EXPECT "1" afr_get_split_brain_count $V0

cleanup;
