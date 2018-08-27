#!/bin/bash
#Self-heal tests

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

#Check that when quorum is not enabled name-heal happens correctly
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST touch $M0/a
TEST touch $M0/c
TEST kill_brick $V0 $H0 $B0/brick0
TEST touch $M0/b
TEST rm -f $M0/a
TEST rm -f $M0/c
TEST touch $M0/c #gfid mismatch case
c_gfid=$(gf_get_gfid_xattr $B0/brick1/c)
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST ! stat $M0/a
TEST ! stat $B0/brick0/a
TEST ! stat $B0/brick1/a

TEST stat $M0/b
TEST stat $B0/brick0/b
TEST stat $B0/brick1/b
TEST [[ "$(gf_get_gfid_xattr $B0/brick0/b)" == "$(gf_get_gfid_xattr $B0/brick1/b)" ]]

TEST stat $M0/c
TEST stat $B0/brick0/c
TEST stat $B0/brick1/c
TEST [[ "$(gf_get_gfid_xattr $B0/brick0/c)" == "$c_gfid" ]]

cleanup;

#Check that when quorum is enabled name-heal happens as expected
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST touch $M0/a
TEST touch $M0/c
TEST kill_brick $V0 $H0 $B0/brick0
TEST touch $M0/b
TEST rm -f $M0/a
TEST rm -f $M0/c
TEST touch $M0/c #gfid mismatch case
c_gfid=$(gf_get_gfid_xattr $B0/brick1/c)
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST ! stat $M0/a
TEST ! stat $B0/brick0/a
TEST ! stat $B0/brick1/a
TEST ! stat $B0/brick2/a

TEST stat $M0/b
TEST ! stat $B0/brick0/b #Name heal shouldn't be triggered
TEST stat $B0/brick1/b
TEST stat $B0/brick2/b

TEST stat $M0/c
TEST stat $B0/brick0/c
TEST stat $B0/brick1/c
TEST stat $B0/brick2/c
TEST [[ "$(gf_get_gfid_xattr $B0/brick0/c)" == "$c_gfid" ]]

TEST $CLI volume set $V0 cluster.quorum-type none
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "none" get_quorum_type $M0 $V0 0
TEST stat $M0/b
TEST stat $B0/brick0/b #Name heal should be triggered
TEST stat $B0/brick1/b
TEST stat $B0/brick2/b
TEST [[ "$(gf_get_gfid_xattr $B0/brick0/b)" == "$(gf_get_gfid_xattr $B0/brick1/b)" ]]
TEST $CLI volume set $V0 cluster.quorum-type auto
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "auto" get_quorum_type $M0 $V0 0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Missing parent xattrs cases
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST $CLI volume heal $V0 disable
#In cases where a good parent doesn't have pending xattrs and a file,
#name-heal will be triggered
TEST gf_rm_file_and_gfid_link $B0/brick1 c
TEST stat $M0/c
TEST stat $B0/brick0/c
TEST stat $B0/brick1/c
TEST stat $B0/brick2/c
TEST [[ "$(gf_get_gfid_xattr $B0/brick0/c)" == "$c_gfid" ]]
cleanup
