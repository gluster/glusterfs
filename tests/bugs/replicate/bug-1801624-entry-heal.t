#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0,1,2}
TEST $CLI volume set $V0 heal-timeout 5
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0 granular-entry-heal enable

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
echo "Data">$M0/FILE
ret=$?
TEST [ $ret -eq 0 ]

# Re-create the file when a brick is down.
TEST kill_brick $V0 $H0 $B0/brick1
TEST rm $M0/FILE
echo "New Data">$M0/FILE
ret=$?
TEST [ $ret -eq 0 ]
EXPECT_WITHIN $HEAL_TIMEOUT "4" get_pending_heal_count $V0

# Launching index heal must not reset parent dir afr xattrs or remove granular entry indices.
$CLI volume heal $V0 # CLI will fail but heal is launched anyway.
TEST sleep 5 # give index heal a chance to do one run.
brick0_pending=$(get_hex_xattr trusted.afr.$V0-client-1 $B0/brick0/)
brick2_pending=$(get_hex_xattr trusted.afr.$V0-client-1 $B0/brick2/)
TEST [ $brick0_pending -eq "000000000000000000000002" ]
TEST [ $brick2_pending -eq "000000000000000000000002" ]
EXPECT "FILE" ls $B0/brick0/.glusterfs/indices/entry-changes/00000000-0000-0000-0000-000000000001/
EXPECT "FILE" ls $B0/brick2/.glusterfs/indices/entry-changes/00000000-0000-0000-0000-000000000001/

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
$CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

# No gfid-split-brain (i.e. EIO) must be seen. Try on fresh mount to avoid cached values.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST cat $M0/FILE

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup;
