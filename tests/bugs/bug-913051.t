#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fileio.rc

cleanup;

#Test that afr opens the file on the bricks that were offline at the time of
# open after the brick comes online. This tests for writev, readv triggering
# open-fd-fix in afr.
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=enable
TEST kill_brick $V0 $H0 $B0/${V0}0

TEST mkdir $M0/dir
TEST touch $M0/dir/a
TEST touch $M0/dir/b
echo abc > $M0/dir/b

TEST wfd=`fd_available`
TEST fd_open $wfd "w" $M0/dir/a
TEST rfd=`fd_available`
TEST fd_open $rfd "r" $M0/dir/b

TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0

#check that the files are not opned on brick-0
realpatha=$(gf_get_gfid_backend_file_path $B0/${V0}0 "dir/a")
EXPECT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 "$realpatha"
EXPECT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 $B0/${V0}0/dir/a

realpathb=$(gf_get_gfid_backend_file_path $B0/${V0}0 "dir/b")
EXPECT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 "$realpathb"
EXPECT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 $B0/${V0}0/dir/b

#attempt self-heal so that the files are created on brick-0

TEST ls -l $M0/dir/a
TEST ls -l $M0/dir/b

#trigger writev for attempting open-fd-fix in afr
TEST fd_write $wfd "open sesame"

#trigger readv for attempting open-fd-fix in afr
TEST fd_cat $rfd

EXPECT_WITHIN 20 "Y" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 $B0/${V0}0/dir/a
EXPECT_WITHIN 20 "Y" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 $B0/${V0}0/dir/b

TEST fd_close $wfd
TEST fd_close $rfd
cleanup;
