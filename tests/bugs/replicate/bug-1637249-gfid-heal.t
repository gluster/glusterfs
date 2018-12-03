#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1};
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume start $V0;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

###############################################################################

# Test for gfid + name heal when there is no 'source' brick, i.e. parent dir
# xattrs are in split-brain or have dirty xattrs.

TEST mkdir $M0/dir_pending
TEST dd if=/dev/urandom of=$M0/dir_pending/file1 bs=1024 count=1024
TEST mkdir $M0/dir_pending/dir11
TEST mkdir $M0/dir_dirty
TEST touch $M0/dir_dirty/file2

# Set pending entry xattrs on dir_pending and remove gfid of entries under it on one brick.
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}0/dir_pending
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/${V0}1/dir_pending

gfid_f1=$(gf_get_gfid_xattr $B0/${V0}0/dir_pending/file1)
gfid_str_f1=$(gf_gfid_xattr_to_str $gfid_f1)
TEST setfattr -x trusted.gfid $B0/${V0}1/dir_pending/file1
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_f1:0:2}/${gfid_str_f1:2:2}/$gfid_str_f1

gfid_d11=$(gf_get_gfid_xattr $B0/${V0}0/dir_pending/dir11)
gfid_str_d11=$(gf_gfid_xattr_to_str $gfid_d11)
TEST setfattr -x trusted.gfid $B0/${V0}1/dir_pending/dir11
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_d11:0:2}/${gfid_str_d11:2:2}/$gfid_str_d11


# Set dirty entry xattrs on dir_dirty and remove gfid of entries under it on one brick.
TEST setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}1/dir_dirty
gfid_f2=$(gf_get_gfid_xattr $B0/${V0}0/dir_dirty/file2)
gfid_str_f2=$(gf_gfid_xattr_to_str $gfid_f2)
TEST setfattr -x trusted.gfid $B0/${V0}1/dir_dirty/file2
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_f2:0:2}/${gfid_str_f2:2:2}/$gfid_str_f2

# Create a file under dir_pending directly on the backend only on 1 brick
TEST touch $B0/${V0}1/dir_pending/file3

# Create a file under dir_pending directly on the backend on all bricks
TEST touch $B0/${V0}0/dir_pending/file4
TEST touch $B0/${V0}1/dir_pending/file4

# Stop  & start the volume and mount client again.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop  $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST stat $M0/dir_pending/file1
EXPECT "$gfid_f1" gf_get_gfid_xattr $B0/${V0}1/dir_pending/file1
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_f1:0:2}/${gfid_str_f1:2:2}/$gfid_str_f1

TEST stat $M0/dir_pending/dir11
EXPECT "$gfid_d11" gf_get_gfid_xattr $B0/${V0}1/dir_pending/dir11
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_d11:0:2}/${gfid_str_d11:2:2}/$gfid_str_d11


TEST stat $M0/dir_dirty/file2
EXPECT "$gfid_f2" gf_get_gfid_xattr $B0/${V0}1/dir_dirty/file2
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_f2:0:2}/${gfid_str_f2:2:2}/$gfid_str_f2

TEST stat $M0/dir_pending/file3 # This assigns gfid on 2nd brick and heals the entry on to the 1st brick.
gfid_f3=$(gf_get_gfid_xattr $B0/${V0}1/dir_pending/file3)
TEST [ ! -z "$gfid_f3" ]
EXPECT "$gfid_f3" gf_get_gfid_xattr $B0/${V0}0/dir_pending/file3

TEST stat $M0/dir_pending/file4
gfid_f4=$(gf_get_gfid_xattr $B0/${V0}0/dir_pending/file4)
TEST [ ! -z "$gfid_f4" ]
EXPECT "$gfid_f4" gf_get_gfid_xattr $B0/${V0}1/dir_pending/file4
###############################################################################

# Test for gfid + name heal when all bricks are 'source', i.e. parent dir
# does not have any pending or dirty xattrs.

TEST mkdir $M0/dir_clean
TEST dd if=/dev/urandom of=$M0/dir_clean/file1 bs=1024 count=1024
TEST mkdir $M0/dir_clean/dir11

gfid_f1=$(gf_get_gfid_xattr $B0/${V0}0/dir_clean/file1)
gfid_str_f1=$(gf_gfid_xattr_to_str $gfid_f1)
TEST setfattr -x trusted.gfid $B0/${V0}1/dir_clean/file1
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_f1:0:2}/${gfid_str_f1:2:2}/$gfid_str_f1

gfid_d11=$(gf_get_gfid_xattr $B0/${V0}0/dir_clean/dir11)
gfid_str_d11=$(gf_gfid_xattr_to_str $gfid_d11)
TEST setfattr -x trusted.gfid $B0/${V0}1/dir_clean/dir11
TEST rm $B0/${V0}1/.glusterfs/${gfid_str_d11:0:2}/${gfid_str_d11:2:2}/$gfid_str_d11

# Create a file under dir_clean directly on the backend only on 1 brick
TEST touch $B0/${V0}1/dir_clean/file3

# Create a file under dir_clean directly on the backend on all bricks
TEST touch $B0/${V0}0/dir_clean/file4
TEST touch $B0/${V0}1/dir_clean/file4

# Stop  & start the volume and mount client again.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop  $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST stat $M0/dir_clean/file1
EXPECT "$gfid_f1" gf_get_gfid_xattr $B0/${V0}1/dir_clean/file1
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_f1:0:2}/${gfid_str_f1:2:2}/$gfid_str_f1

TEST stat $M0/dir_clean/dir11
EXPECT "$gfid_d11" gf_get_gfid_xattr $B0/${V0}1/dir_clean/dir11
TEST stat $B0/${V0}1/.glusterfs/${gfid_str_d11:0:2}/${gfid_str_d11:2:2}/$gfid_str_d11

TEST stat $M0/dir_clean/file3 # This assigns gfid on 2nd brick and heals the entry on to the 1st brick.
gfid_f3=$(gf_get_gfid_xattr $B0/${V0}1/dir_clean/file3)
TEST [ ! -z "$gfid_f3" ]
EXPECT "$gfid_f3" gf_get_gfid_xattr $B0/${V0}0/dir_clean/file3

TEST stat $M0/dir_clean/file4
gfid_f4=$(gf_get_gfid_xattr $B0/${V0}0/dir_clean/file4)
TEST [ ! -z "$gfid_f4" ]
EXPECT "$gfid_f4" gf_get_gfid_xattr $B0/${V0}1/dir_clean/file4
###############################################################################

cleanup;
