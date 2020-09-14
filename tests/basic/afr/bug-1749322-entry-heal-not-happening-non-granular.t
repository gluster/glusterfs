#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup

function check_gfid_and_link_count
{
        local file=$1

        file_gfid_b0=$(gf_get_gfid_xattr $B0/${V0}0/$file)
        TEST [ ! -z $file_gfid_b0 ]
        file_gfid_b1=$(gf_get_gfid_xattr $B0/${V0}1/$file)
        file_gfid_b2=$(gf_get_gfid_xattr $B0/${V0}2/$file)
        EXPECT $file_gfid_b0 echo $file_gfid_b1
        EXPECT $file_gfid_b0 echo $file_gfid_b2

        EXPECT "2" stat -c %h $B0/${V0}0/$file
        EXPECT "2" stat -c %h $B0/${V0}1/$file
        EXPECT "2" stat -c %h $B0/${V0}2/$file
}
TESTS_EXPECTED_IN_LOOP=18

################################################################################
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
TEST `echo "File 1 " > $M0/dir/file1`
TEST touch $M0/dir/file{2..4}

# Remove file2 from 1st & 3rd bricks
TEST rm -f $B0/$V0"0"/dir/file2
TEST rm -f $B0/$V0"2"/dir/file2

# Remove file3 and the .glusterfs hardlink from 1st & 2nd bricks
gfid_file3=$(gf_get_gfid_xattr $B0/$V0"0"/dir/file3)
gfid_str_file3=$(gf_gfid_xattr_to_str $gfid_file3)
TEST rm $B0/$V0"0"/.glusterfs/${gfid_str_file3:0:2}/${gfid_str_file3:2:2}/$gfid_str_file3
TEST rm $B0/$V0"1"/.glusterfs/${gfid_str_file3:0:2}/${gfid_str_file3:2:2}/$gfid_str_file3
TEST rm -f $B0/$V0"0"/dir/file3
TEST rm -f $B0/$V0"1"/dir/file3

# Remove the .glusterfs hardlink and the gfid xattr of file4 on 3rd brick
gfid_file4=$(gf_get_gfid_xattr $B0/$V0"0"/dir/file4)
gfid_str_file4=$(gf_gfid_xattr_to_str $gfid_file4)
TEST rm $B0/$V0"2"/.glusterfs/${gfid_str_file4:0:2}/${gfid_str_file4:2:2}/$gfid_str_file4
TEST setfattr -x trusted.gfid $B0/$V0"2"/dir/file4

# B0 and B2 blame each other
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/$V0"2"/dir
setfattr -n trusted.afr.$V0-client-2 -v 0x000000000000000000000001 $B0/$V0"0"/dir

# Add entry to xattrop dir on first brick.
xattrop_dir0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_dir0`
gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/dir/))
TEST ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_str

EXPECT "^1$" get_pending_heal_count $V0

# Launch heal
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^Y$" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

# All the files must be present on all the bricks after conservative merge and
# should have the gfid xattr and the .glusterfs hardlink.
check_gfid_and_link_count dir/file1
check_gfid_and_link_count dir/file2
check_gfid_and_link_count dir/file3
check_gfid_and_link_count dir/file4

cleanup
