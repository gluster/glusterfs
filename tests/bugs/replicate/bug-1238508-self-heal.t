#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

# Disable self-heal-daemon
TEST $CLI volume set $V0 cluster.self-heal-daemon off

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST mkdir $M0/olddir;
TEST `echo "some-data" > $M0/olddir/oldfile`

TEST kill_brick $V0 $H0 $B0/${V0}1
TEST mv $M0/olddir/oldfile $M0/olddir/newfile;
TEST mv $M0/olddir $M0/newdir;

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

# Test if the files are present on both the bricks
EXPECT "newdir" ls $B0/${V0}0/
EXPECT "newdir" ls $B0/${V0}1/
EXPECT "newfile" ls $B0/${V0}0/newdir/
EXPECT "newfile" ls $B0/${V0}1/newdir/

# Test if gfid-link files in .glusterfs also provide correct info
brick0gfid=$(gf_get_gfid_backend_file_path $B0/${V0}0 newdir)
brick1gfid=$(gf_get_gfid_backend_file_path $B0/${V0}1 newdir)
EXPECT "newfile" ls $brick0gfid
EXPECT "newfile" ls $brick1gfid

# Test if the files are accessible from the mount
EXPECT "newdir" ls $M0/
EXPECT "newfile" ls $M0/newdir

cleanup;
