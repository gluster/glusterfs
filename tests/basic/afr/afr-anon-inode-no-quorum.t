#!/bin/bash

#Test that anon-inode entry is not cleaned up as long as there exists at least
#one valid entry
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.readdir-ahead off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST touch $M0/a $M0/b

gfid_a=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/a))
gfid_b=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/b))
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST mv $M0/a $M0/a-new
TEST mv $M0/b $M0/b-new

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST ! ls $M0/a
TEST ! ls $M0/b
anon_inode_name=$(ls -a $B0/${V0}0 | grep glusterfs-anonymous-inode)
TEST stat $B0/${V0}0/$anon_inode_name/$gfid_a
TEST stat $B0/${V0}0/$anon_inode_name/$gfid_b
#Make sure index heal doesn't happen after enabling heal
TEST setfattr -x trusted.afr.$V0-client-0 $B0/${V0}1
TEST rm -f $B0/${V0}1/.glusterfs/indices/xattrop/*
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
#Allow time for a scan
sleep 5
TEST stat $B0/${V0}0/$anon_inode_name/$gfid_a
TEST stat $B0/${V0}0/$anon_inode_name/$gfid_b
inum_b=$(STAT_INO $B0/${V0}0/$anon_inode_name/$gfid_b)
TEST rm -f $M0/a-new
TEST stat $M0/b-new

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/${V0}0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" afr_anon_entry_count $B0/${V0}1
EXPECT "$inum_b" STAT_INO $B0/${V0}0/b-new

cleanup
