#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST $CLI volume set $V0 self-heal-daemon off

# Create base entry in indices/xattrop
echo "Data" > $M0/FILE

#------------------------------------------------------------------------------#
TEST touch $M0/f1
gfid_f1=$(gf_get_gfid_xattr $B0/${V0}0/f1)
gfid_str_f1=$(gf_gfid_xattr_to_str $gfid_f1)

# Remove gfid xattr and .glusterfs hard link from 2nd brick. This simulates a
# brick crash at the point where file got created but no xattrs were set.
TEST setfattr -x trusted.gfid $B0/${V0}1/f1
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_f1:0:2}/${gfid_str_f1:2:2}/$gfid_str_f1

# Assume there were no pending xattrs on parent dir due to 1st brick crashing
# too. Then name heal from client must heal the gfid.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST stat $M0/f1
EXPECT "$gfid_f1" gf_get_gfid_xattr $B0/${V0}1/f1
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_f1:0:2}/${gfid_str_f1:2:2}/$gfid_str_f1

#------------------------------------------------------------------------------#
TEST mkdir $M0/dir
TEST touch $M0/dir/f2
gfid_f2=$(gf_get_gfid_xattr $B0/${V0}0/dir/f2)
gfid_str_f2=$(gf_gfid_xattr_to_str $gfid_f2)

# Remove gfid xattr and .glusterfs hard link from 2nd brick. This simulates a
# brick crash at the point where file got created but no xattrs were set.
TEST setfattr -x trusted.gfid $B0/${V0}1/dir/f2
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_f2:0:2}/${gfid_str_f2:2:2}/$gfid_str_f2

#Now simulate setting of pending entry xattr on parent dir of 1st brick.
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}0/dir
create_brick_xattrop_entry $B0/${V0}0 dir

#Trigger entry-heal via shd
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT "$gfid_f2" gf_get_gfid_xattr $B0/${V0}1/dir/f2
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_f2:0:2}/${gfid_str_f2:2:2}/$gfid_str_f2

#------------------------------------------------------------------------------#
cleanup;
