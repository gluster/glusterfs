#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST kill_brick $V0 $H0 $B0/${V0}2
# Create base entry in indices/xattrop
echo "Data" > $M0/file
EXPECT "^3$" count_index_entries $B0/$V0"0"

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
EXPECT "^1$" count_index_entries $B0/$V0"0"


# Simulate a stale indices entry by manually creating a link to xattrop base
# entry with an existing entry which does not need any heal
TEST create_brick_xattrop_entry $B0/$V0"0" file
EXPECT "^2$" count_index_entries $B0/$V0"0"
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

cleanup;
