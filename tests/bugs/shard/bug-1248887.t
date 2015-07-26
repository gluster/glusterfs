#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
#Create a file.
TEST touch $M0/foo
#Write some data into it.
TEST `echo "abc" > $M0/foo`
EXPECT "0000000000400000" get_hex_xattr trusted.glusterfs.shard.block-size $B0/${V0}0/foo
EXPECT "0000000000000004000000000000000000000000000000010000000000000000" get_hex_xattr trusted.glusterfs.shard.file-size $B0/${V0}0/foo
EXPECT "0000000000400000" get_hex_xattr trusted.glusterfs.shard.block-size $B0/${V0}1/foo
EXPECT "0000000000000004000000000000000000000000000000010000000000000000" get_hex_xattr trusted.glusterfs.shard.file-size $B0/${V0}1/foo
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST `echo "abc" >> $M0/foo`
EXPECT "0000000000400000" get_hex_xattr trusted.glusterfs.shard.block-size $B0/${V0}1/foo
EXPECT "0000000000000008000000000000000000000000000000010000000000000000" get_hex_xattr trusted.glusterfs.shard.file-size $B0/${V0}1/foo
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
EXPECT "0000000000400000" get_hex_xattr trusted.glusterfs.shard.block-size $B0/${V0}0/foo
EXPECT "0000000000000008000000000000000000000000000000010000000000000000" get_hex_xattr trusted.glusterfs.shard.file-size $B0/${V0}0/foo
EXPECT "0000000000400000" get_hex_xattr trusted.glusterfs.shard.block-size $B0/${V0}1/foo
EXPECT "0000000000000008000000000000000000000000000000010000000000000000" get_hex_xattr trusted.glusterfs.shard.file-size $B0/${V0}1/foo

cleanup;
