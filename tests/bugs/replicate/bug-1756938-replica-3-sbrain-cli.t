#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard enable
TEST $CLI volume set $V0 features.shard-block-size 4MB

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST glusterfs --volfile-server=$H0 --volfile-id=/$V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

#Create split-brain by setting afr xattrs/gfids manually.
#file1 is non-sharded and will be in data split-brain.
#file2 will have one shard which will be in data split-brain.
#file3 will have one shard which will be in gfid split-brain.
#file4 will have one shard which will be in data & metadata split-brain.
TEST dd if=/dev/zero of=$M0/file1 bs=1024 count=1024 oflag=direct
TEST dd if=/dev/zero of=$M0/file2 bs=1M count=6 oflag=direct
TEST dd if=/dev/zero of=$M0/file3 bs=1M count=6 oflag=direct
TEST dd if=/dev/zero of=$M0/file4 bs=1M count=6 oflag=direct
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#-------------------------------------------------------------------------------
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000000000000 $B0/${V0}0/file1
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000000000000 $B0/${V0}0/file1
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000000000000 $B0/${V0}1/file1
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000000000000 $B0/${V0}1/file1
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000000000000 $B0/${V0}2/file1
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000000000000 $B0/${V0}2/file1

#-------------------------------------------------------------------------------
gfid_f2=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/file2))
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000000000000 $B0/${V0}0/.shard/$gfid_f2.1
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000000000000 $B0/${V0}0/.shard/$gfid_f2.1
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000000000000 $B0/${V0}1/.shard/$gfid_f2.1
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000000000000 $B0/${V0}1/.shard/$gfid_f2.1
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000000000000 $B0/${V0}2/.shard/$gfid_f2.1
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000000000000 $B0/${V0}2/.shard/$gfid_f2.1

#-------------------------------------------------------------------------------
TESTS_EXPECTED_IN_LOOP=5
function assign_new_gfid {
    brickpath=$1
    filename=$2
    gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $brickpath/$filename))
    gfid_shard=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $brickpath/.shard/$gfid.1))

    TEST rm $brickpath/.glusterfs/${gfid_shard:0:2}/${gfid_shard:2:2}/$gfid_shard
    TEST setfattr -x trusted.gfid $brickpath/.shard/$gfid.1
    new_gfid=$(get_random_gfid)
    new_gfid_str=$(gf_gfid_xattr_to_str $new_gfid)
    TEST setfattr -n trusted.gfid -v $new_gfid $brickpath/.shard/$gfid.1
    TEST mkdir -p $brickpath/.glusterfs/${new_gfid_str:0:2}/${new_gfid_str:2:2}
    TEST ln $brickpath/.shard/$gfid.1 $brickpath/.glusterfs/${new_gfid_str:0:2}/${new_gfid_str:2:2}/$new_gfid_str
}
assign_new_gfid $B0/$V0"1" file3
assign_new_gfid $B0/$V0"2" file3

#-------------------------------------------------------------------------------
gfid_f4=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/file4))
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000100000000 $B0/${V0}0/.shard/$gfid_f4.1
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000100000000 $B0/${V0}0/.shard/$gfid_f4.1
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000100000000 $B0/${V0}1/.shard/$gfid_f4.1
TEST setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000100000000 $B0/${V0}1/.shard/$gfid_f4.1
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000100000000 $B0/${V0}2/.shard/$gfid_f4.1
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000100000000 $B0/${V0}2/.shard/$gfid_f4.1

#-------------------------------------------------------------------------------
#Add entry to xattrop dir on first brick and check for split-brain.
xattrop_dir0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_dir0`

gfid_f1=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/file1))
TEST ln  $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_f1

gfid_f2_shard1=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/.shard/$gfid_f2.1))
TEST ln  $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_f2_shard1

gfid_f3=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/file3))
gfid_f3_shard1=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/.shard/$gfid_f3.1))
TEST ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_f3_shard1

gfid_f4_shard1=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/.shard/$gfid_f4.1))
TEST ln  $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_f4_shard1

#-------------------------------------------------------------------------------
#gfid split-brain won't show up in split-brain count.
EXPECT "3" afr_get_split_brain_count $V0
EXPECT_NOT "^0$" get_pending_heal_count $V0

#Resolve split-brains
TEST $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}1 /file1
GFIDSTR="gfid:$gfid_f2_shard1"
TEST $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}1 $GFIDSTR
TEST $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}1 /.shard/$gfid_f3.1
TEST $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}1 /.shard/$gfid_f4.1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
cleanup;
