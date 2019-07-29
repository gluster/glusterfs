#!/bin/bash
#
# This will test healing of ctime xattr 'trusted.glusterfs.mdata' after add-brick and rebalance
#
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

cleanup

#cleate and start volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0

#Mount the volume
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

# Create files
mkdir $M0/dir1
echo "test data" > $M0/dir1/file1

# Add brick
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{6..8}

#Trigger rebalance
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

#Verify ctime xattr heal on directory
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}6/dir1"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}7/dir1"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.glusterfs.mdata' check_for_xattr 'trusted.glusterfs.mdata' "$B0/${V0}8/dir1"

b6_mdata=$(get_mdata "$B0/${V0}6/dir1")
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "${b6_mdata}" get_mdata $B0/${V0}7/dir1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "${b6_mdata}" get_mdata $B0/${V0}8/dir1

cleanup;
