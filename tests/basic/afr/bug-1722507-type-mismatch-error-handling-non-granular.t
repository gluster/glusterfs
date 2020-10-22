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
TEST $CLI volume set $V0 cluster.granular-entry-heal off
TEST $CLI volume start $V0;
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume heal $V0 disable
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir

##########################################################################################
# GFID link file and the GFID is missing on one brick and all the bricks are being blamed.

TEST touch $M0/dir/file
TEST `echo append>> $M0/dir/file`

#B0 and B2 must blame B1
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/$V0"2"/dir
setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/$V0"0"/dir
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000000000001 $B0/$V0"0"/dir

# Add entry to xattrop dir to trigger index heal.
xattrop_dir0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_dir0`
gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/dir/))
ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_str
EXPECT "^1$" get_pending_heal_count $V0

# Remove the gfid xattr and the link file on one brick.
gfid_file=$(gf_get_gfid_xattr $B0/$V0"0"/dir/file)
gfid_str_file=$(gf_gfid_xattr_to_str $gfid_file)
TEST setfattr -x trusted.gfid $B0/${V0}0/dir/file
TEST rm -f $B0/${V0}0/.glusterfs/${gfid_str_file:0:2}/${gfid_str_file:2:2}/$gfid_str_file

# Launch heal
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^Y$" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2

# Wait for 2 second to force posix to consider that this is a valid file but
# without gfid.
sleep 2
TEST $CLI volume heal $V0

# Heal should not fail as the file is missing gfid xattr and the link file,
# which is not actually the gfid or type mismatch.
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT "$gfid_file" gf_get_gfid_xattr $B0/${V0}0/dir/file
TEST stat $B0/${V0}0/.glusterfs/${gfid_str_file:0:2}/${gfid_str_file:2:2}/$gfid_str_file
rm -f $M0/dir/file


###########################################################################################
# GFID link file and the GFID is missing on two bricks and all the bricks are being blamed.

TEST $CLI volume heal $V0 disable
TEST touch $M0/dir/file
#TEST kill_brick $V0 $H0 $B0/$V0"1"

#B0 and B2 must blame B1
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/$V0"2"/dir
setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/$V0"0"/dir
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000000000001 $B0/$V0"0"/dir

# Add entry to xattrop dir to trigger index heal.
xattrop_dir0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_dir0`
gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/dir/))
ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_str
EXPECT "^1$" get_pending_heal_count $V0

# Remove the gfid xattr and the link file on two bricks.
gfid_file=$(gf_get_gfid_xattr $B0/$V0"0"/dir/file)
gfid_str_file=$(gf_gfid_xattr_to_str $gfid_file)
TEST setfattr -x trusted.gfid $B0/${V0}0/dir/file
TEST rm -f $B0/${V0}0/.glusterfs/${gfid_str_file:0:2}/${gfid_str_file:2:2}/$gfid_str_file
TEST setfattr -x trusted.gfid $B0/${V0}1/dir/file
TEST rm -f $B0/${V0}1/.glusterfs/${gfid_str_file:0:2}/${gfid_str_file:2:2}/$gfid_str_file

# Launch heal
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^Y$" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2

# Wait for 2 second to force posix to consider that this is a valid file but
# without gfid.
sleep 2
TEST $CLI volume heal $V0

# Heal should not fail as the file is missing gfid xattr and the link file,
# which is not actually the gfid or type mismatch.
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT "$gfid_file" gf_get_gfid_xattr $B0/${V0}0/dir/file
TEST stat $B0/${V0}0/.glusterfs/${gfid_str_file:0:2}/${gfid_str_file:2:2}/$gfid_str_file
EXPECT "$gfid_file" gf_get_gfid_xattr $B0/${V0}1/dir/file
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_file:0:2}/${gfid_str_file:2:2}/$gfid_str_file

cleanup
