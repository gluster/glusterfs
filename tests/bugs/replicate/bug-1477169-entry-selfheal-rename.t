#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST mkdir -p $M0/d1/dir012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789 $M0/d2
gfid_d1=$(gf_get_gfid_xattr $B0/${V0}0/d1)
gfid_d2=$(gf_get_gfid_xattr $B0/${V0}0/d2)
gfid_dir=$(gf_get_gfid_xattr $B0/${V0}0/d1/dir012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789)

gfid_str_d1=$(gf_gfid_xattr_to_str $gfid_d1)
gfid_str_d2=$(gf_gfid_xattr_to_str $gfid_d2)
gfid_str_d3=$(gf_gfid_xattr_to_str $gfid_dir)

# Kill 3rd brick and rename the dir from mount.
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST mv $M0/d1/dir012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789 $M0/d2

# Bring it back and trigger heal.
TEST $CLI volume start $V0 force

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# Check that .glusterfs symlink for dir exists and points to d2/dir012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
TEST linkname=$(readlink $B0/${V0}2/.glusterfs/${gfid_str_d3:0:2}/${gfid_str_d3:2:2}/$gfid_str_d3)
EXPECT "dir012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789" basename $linkname
TEST parent_dir_gfid_str=$(echo $linkname|cut -d / -f5)
EXPECT $gfid_str_d2 echo $parent_dir_gfid_str

TEST rmdir $M0/d2/dir012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789

TEST ! stat $B0/${V0}0/.glusterfs/${gfid_str_d3:0:2}/${gfid_str_d3:2:2}/$gfid_str_d3
TEST ! stat $B0/${V0}1/.glusterfs/${gfid_str_d3:0:2}/${gfid_str_d3:2:2}/$gfid_str_d3
TEST ! stat $B0/${V0}2/.glusterfs/${gfid_str_d3:0:2}/${gfid_str_d3:2:2}/$gfid_str_d3
cleanup;
