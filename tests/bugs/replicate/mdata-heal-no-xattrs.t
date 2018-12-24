#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
echo "Data">$M0/FILE
ret=$?
TEST [ $ret -eq 0 ]

# Change permission on brick-0: simulates the case where there is metadata
# mismatch but no pending xattrs. This brick will become the source for heal.
TEST chmod +x $B0/$V0"0"/FILE

# Add gfid to xattrop
xattrop_b0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_b0`
gfid_str_FILE=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/FILE))
TEST ln $xattrop_b0/$base_entry_b0 $xattrop_b0/$gfid_str_FILE
EXPECT_WITHIN $HEAL_TIMEOUT "^1$" get_pending_heal_count $V0

TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Brick-0 should contain xattrs blaming other 2 bricks.
# The values will be zero because heal is over.
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0/FILE
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}0/FILE
TEST ! getfattr -n trusted.afr.$V0-client-0 $B0/${V0}0/FILE

# Brick-1 and Brick-2 must not contain any afr xattrs.
TEST ! getfattr -n trusted.afr.$V0-client-0 $B0/${V0}1/FILE
TEST ! getfattr -n trusted.afr.$V0-client-1 $B0/${V0}1/FILE
TEST ! getfattr -n trusted.afr.$V0-client-2 $B0/${V0}1/FILE
TEST ! getfattr -n trusted.afr.$V0-client-0 $B0/${V0}2/FILE
TEST ! getfattr -n trusted.afr.$V0-client-1 $B0/${V0}2/FILE
TEST ! getfattr -n trusted.afr.$V0-client-2 $B0/${V0}2/FILE

# check permission bits.
EXPECT '755' stat -c %a $B0/${V0}0/FILE
EXPECT '755' stat -c %a $B0/${V0}1/FILE
EXPECT '755' stat -c %a $B0/${V0}2/FILE

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup;
