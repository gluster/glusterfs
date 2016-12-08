#!/bin/bash

#Tests for files without gfids

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 nfs.disable on
TEST touch $B0/${V0}{0,1}/{1,2,3,4}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
#Test that readdir returns entries even when no gfids are present
EXPECT 4 echo $(ls $M0 | grep -v '^\.' | wc -l)
sleep 2;
#stat the files and check that the files have same gfids on the bricks now
TEST stat $M0/1
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/1)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/1)
TEST "[[ ! -z $gfid_0 ]]"
EXPECT $gfid_0 echo $gfid_1

TEST stat $M0/2
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/2)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/2)
TEST "[[ ! -z $gfid_0 ]]"
EXPECT $gfid_0 echo $gfid_1

TEST stat $M0/3
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/3)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/3)
TEST "[[ ! -z $gfid_0 ]]"
EXPECT $gfid_0 echo $gfid_1

TEST stat $M0/4
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/4)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/4)
TEST "[[ ! -z $gfid_0 ]]"
EXPECT $gfid_0 echo $gfid_1

#Check gfid self-heal happens from one brick to other when a file has missing
#gfid
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/a
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/a)
TEST touch $B0/${V0}0/a
$CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST stat $M0/a
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/a)
EXPECT $gfid_1 echo $gfid_0

#Check gfid self-heal doesn't happen from one brick to other when type mismatch
#is present for a name, without any xattrs
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/b
TEST mkdir $B0/${V0}0/b
TEST setfattr -x trusted.afr.$V0-client-0 $B0/${V0}1
$CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST ! stat $M0/b
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/b)
TEST "[[ -z \"$gfid_0\" ]]"

#Check gfid assigning doesn't happen when there is type mismatch
TEST touch $B0/${V0}1/c
TEST mkdir $B0/${V0}0/c
TEST ! stat $M0/c
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/c)
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/c)
TEST "[[ -z \"$gfid_1\" ]]"
TEST "[[ -z \"$gfid_0\" ]]"

#Check gfid assigning doesn't happen only when even one brick is down to prevent
# gfid split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $B0/${V0}1/d
TEST ! stat $M0/d
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/d)
TEST "[[ -z \"$gfid_1\" ]]"
TEST $CLI volume start $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

#Check gfid self-heal doesn't happen from one brick to other when type mismatch
#is present for a name without any pending xattrs
#TEST kill_brick $V0 $H0 $B0/${V0}0
#TEST touch $M0/e
#TEST $CLI volume start $V0 force;
#EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
#TEST kill_brick $V0 $H0 $B0/${V0}1
#TEST mkdir $M0/e
#TEST $CLI volume stop $V0 force;
#TEST setfattr -x trusted.gfid $B0/${V0}1/e
#TEST setfattr -x trusted.gfid $B0/${V0}0/e
#TEST $CLI volume set $V0 cluster.entry-self-heal off
#$CLI volume start $V0 force
#EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
#EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
#TEST ! stat $M0/e
#gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/e)
#gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/e)
#TEST "[[ -z \"$gfid_1\" ]]"
#TEST "[[ -z \"$gfid_0\" ]]"

#Check if lookup fails with gfid-mismatch of a file
#is present for a name without any pending xattrs
#TEST kill_brick $V0 $H0 $B0/${V0}0
#TEST touch $M0/f
#$CLI volume start $V0 force
#EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
#TEST kill_brick $V0 $H0 $B0/${V0}1
#TEST touch $M0/f
#simulate no pending changelog
#$CLI volume stop $V0 force
#TEST setfattr -x trusted.afr.$V0-client-0 $B0/${V0}1
#TEST setfattr -x trusted.afr.$V0-client-1 $B0/${V0}0
#$CLI volume start $V0 force
#EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
#EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
#TEST ! stat $M0/f

cleanup
