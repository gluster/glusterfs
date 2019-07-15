#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume heal $V0 disable
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

###############################################################################
# Case of 2 bricks blaming the third and the third blaming the other two.

TEST mkdir $M0/dir

# B0 and B2 must blame B1
TEST kill_brick $V0 $H0 $B0/$V0"1"
TEST setfattr -n user.metadata -v 1 $M0/dir
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}0/dir trusted.afr.$V0-client-1 metadata
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}2/dir trusted.afr.$V0-client-1 metadata
CLIENT_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $M0/dir)

# B1 must blame B0 and B2
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000100000000 $B0/$V0"1"/dir
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000100000000 $B0/$V0"1"/dir

# Launch heal
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}1
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^Y$" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

B0_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}0/dir)
B1_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}1/dir)
B2_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}2/dir)

TEST [ "$CLIENT_XATTR" == "$B0_XATTR" ]
TEST [ "$CLIENT_XATTR" == "$B1_XATTR" ]
TEST [ "$CLIENT_XATTR" == "$B2_XATTR" ]
TEST setfattr -x user.metadata $M0/dir

###############################################################################
# Case of each brick blaming the next one in a cyclic manner

TEST $CLI volume heal $V0 disable
TEST `echo "hello" >> $M0/dir/file`
# Mark cyclic xattrs and modify metadata directly on the bricks.
setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000100000000 $B0/$V0"0"/dir/file
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000100000000 $B0/$V0"1"/dir/file
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000100000000 $B0/$V0"2"/dir/file

setfattr -n user.metadata -v 1 $B0/$V0"0"/dir/file
setfattr -n user.metadata -v 2 $B0/$V0"1"/dir/file
setfattr -n user.metadata -v 3 $B0/$V0"2"/dir/file

# Add entry to xattrop dir to trigger index heal.
xattrop_dir0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_dir0`
gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/dir/file))
ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_str
EXPECT_WITHIN $HEAL_TIMEOUT "^1$" get_pending_heal_count $V0

# Launch heal
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^Y$" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

B0_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}0/dir/file)
B1_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}1/dir/file)
B2_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}2/dir/file)

TEST [ "$B0_XATTR" == "$B1_XATTR" ]
TEST [ "$B0_XATTR" == "$B2_XATTR" ]
TEST rm -f $M0/dir/file

###############################################################################
# Case of 2 bricks having quorum blaming and the other having only one blaming.

TEST $CLI volume heal $V0 disable
TEST `echo "hello" >> $M0/dir/file`
# B0 and B2 must blame B1
TEST kill_brick $V0 $H0 $B0/$V0"1"
TEST setfattr -n user.metadata -v 1 $M0/dir/file
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}0/dir/file trusted.afr.$V0-client-1 metadata
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}2/dir/file trusted.afr.$V0-client-1 metadata

# B1 must blame B0 and B2
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000100000000 $B0/$V0"1"/dir/file
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000100000000 $B0/$V0"1"/dir/file

# B0 must blame B2
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000100000000 $B0/$V0"0"/dir/file

# Modify the metadata directly on the bricks B1 & B2.
setfattr -n user.metadata -v 2 $B0/$V0"1"/dir/file
setfattr -n user.metadata -v 3 $B0/$V0"2"/dir/file

# Launch heal
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" brick_up_status $V0 $H0 $B0/${V0}1
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^Y$" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

B0_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}0/dir/file)
B1_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}1/dir/file)
B2_XATTR=$(getfattr -n 'user.metadata' --absolute-names --only-values $B0/${V0}2/dir/file)

TEST [ "$B0_XATTR" == "$B1_XATTR" ]
TEST [ "$B0_XATTR" == "$B2_XATTR" ]

###############################################################################

cleanup
