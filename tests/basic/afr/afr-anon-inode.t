#!/bin/bash
#Tests that afr-anon-inode test cases work fine as expected
#These are cases where in entry-heal/name-heal we dont know entry for an inode
#so these inodes are kept in a special directory

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
EXPECT "^1$" afr_private_key_value $V0 $M0 0 "use-anonymous-inode"
TEST $CLI volume set $V0 cluster.use-anonymous-inode no
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^0$" afr_private_key_value $V0 $M0 0 "use-anonymous-inode"
TEST $CLI volume set $V0 cluster.use-anonymous-inode yes
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "use-anonymous-inode"
TEST mkdir -p $M0/d1/b $M0/d2/a
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST mv $M0/d2/a $M0/d1
TEST mv $M0/d1/b $M0/d2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
anon_inode_name=$(ls -a $B0/${V0}0 | grep glusterfs-anonymous-inode)
TEST [[ -d $B0/${V0}1/$anon_inode_name ]]
TEST [[ -d $B0/${V0}2/$anon_inode_name ]]
anon_gfid=$(gf_get_gfid_xattr $B0/${V0}0/$anon_inode_name)
EXPECT "$anon_gfid" gf_get_gfid_xattr $B0/${V0}1/$anon_inode_name
EXPECT "$anon_gfid" gf_get_gfid_xattr $B0/${V0}2/$anon_inode_name

TEST ! ls $M0/$anon_inode_name
EXPECT "^4$" echo $(ls -a $M0 | wc -l)

#Test purging code path by shd
TEST $CLI volume heal $V0 disable
TEST mkdir $M0/l0 $M0/l1 $M0/l2
TEST touch $M0/del-file $M0/del-file-nolink $M0/l0/file
TEST ln $M0/del-file $M0/del-file-link
TEST ln $M0/l0/file $M0/l1/file-link1
TEST ln $M0/l0/file $M0/l2/file-link2
TEST mkdir -p $M0/del-recursive-dir/d1

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST rm -f $M0/del-file $M0/del-file-nolink
TEST rm -rf $M0/del-recursive-dir
TEST mv $M0/d1/a $M0/d2
TEST mv $M0/l0/file $M0/l0/renamed-file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status $V0 0

nolink_gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/del-file-nolink))
link_gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/del-file))
dir_gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/del-recursive-dir))
rename_dir_gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/d1/a))
rename_file_gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/l0/file))
TEST ! stat $M0/del-file
TEST stat $B0/${V0}0/$anon_inode_name/$link_gfid
TEST ! stat $M0/del-file-nolink
TEST ! stat $B0/${V0}0/$anon_inode_name/$nolink_gfid
TEST ! stat $M0/del-recursive-dir
TEST stat $B0/${V0}0/$anon_inode_name/$dir_gfid
TEST ! stat $M0/d1/a
TEST stat $B0/${V0}0/$anon_inode_name/$rename_dir_gfid
TEST ! stat $M0/l0/file
TEST stat $B0/${V0}0/$anon_inode_name/$rename_file_gfid

TEST kill_brick $V0 $H0 $B0/${V0}1
TEST mv $M0/l1/file-link1 $M0/l1/renamed-file-link1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status $V0 1
TEST ! stat $M0/l1/file-link1
TEST stat $B0/${V0}1/$anon_inode_name/$rename_file_gfid

TEST kill_brick $V0 $H0 $B0/${V0}2
TEST mv $M0/l2/file-link2 $M0/l2/renamed-file-link2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status $V0 2
TEST ! stat $M0/l2/file-link2
TEST stat $B0/${V0}2/$anon_inode_name/$rename_file_gfid

#Simulate only anon-inodes present in all bricks
TEST rm -f $M0/l0/renamed-file $M0/l1/renamed-file-link1 $M0/l2/renamed-file-link2

#Test that shd doesn't cleanup anon-inodes when some bricks are down
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST $CLI volume heal $V0 enable
$CLI volume heal $V0
sleep 5 #Allow time for completion of one scan
TEST stat $B0/${V0}0/$anon_inode_name/$link_gfid
TEST stat $B0/${V0}0/$anon_inode_name/$rename_dir_gfid
TEST stat $B0/${V0}0/$anon_inode_name/$dir_gfid
rename_dir_inum=$(STAT_INO $B0/${V0}0/$anon_inode_name/$rename_dir_gfid)

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^1$" afr_child_up_status $V0 1
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/${V0}0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/${V0}1
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/${V0}2

#Test that rename indeed happened instead of rmdir/mkdir
renamed_dir_inum=$(STAT_INO $B0/${V0}0/d2/a)
EXPECT "$rename_dir_inum" echo $renamed_dir_inum
cleanup;
