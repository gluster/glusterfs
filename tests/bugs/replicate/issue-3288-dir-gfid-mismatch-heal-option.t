#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 performance.stat-prefetch off

##### Healing with favorite-child-policy = mtime ######
#####           and self-heal-daemon             ######


## Create a dir and a file and make sure that only file gfid mismatch
## are corrected with option

TEST $CLI volume set $V0 favorite-child-policy mtime
TEST kill_brick $V0 $H0 $B0/${V0}0
echo "Sink based on mtime" > $M0/f2
TEST mkdir $M0/dir2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Source based on mtime" > $M0/f2
TEST mkdir $M0/dir2

#Gfids of file f2 on bricks 0 & 1 should differ
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f2)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f2)
TEST [ "$gfid_0" != "$gfid_1" ]

#Gfids of file dir2 on bricks 0 & 1 should differ
dir_gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/dir2)
dir_gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/dir2)
TEST [ "$dir_gfid_0" != "$dir_gfid_1" ]

TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

#We know that first brick has the latest mtime
LATEST_MTIME=$(stat -c %Y $B0/${V0}0/f2)
LATEST_DIR_MTIME=$(stat -c %Y $B0/${V0}0/dir2)

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
#dir2 won't be healed
EXPECT_WITHIN $HEAL_TIMEOUT "^4$" get_pending_heal_count $V0

#gfid split-brain should be resolved for f2 and not for dir2
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f2)
dir_gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/dir2)
TEST [ "$gfid_0" == "$gfid_1" ]
TEST [ "$dir_gfid_0" != "$dir_gfid_1" ]

HEALED_MTIME=$(stat -c %Y $B0/${V0}1/f2)
HEALED_DIR_MTIME=$(stat -c %Y $B0/${V0}1/dir2)

TEST [ "$LATEST_MTIME" == "$HEALED_MTIME" ]
TEST [ "$LATEST_DIR_MTIME" != "$HEALED_DIR_MTIME" ]

#Try using client side heal

TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on

#Lookup on the dir2 will fail with EIO
TEST ls $M0
TEST ls $M0/f2
#Healing should fail
TEST ! ls $M0/dir2
EXPECT_WITHIN $HEAL_TIMEOUT "^4$" get_pending_heal_count $V0

##### Test GFID mismatch resolution with CLI  ######
#####          and self-heal-daemon           ######

TEST $CLI volume set $V0 self-heal-daemon on

#Just confirm Gfids of dir2 on bricks 0 & 1 should differ
dir_gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/dir2)
dir_gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/dir2)
TEST [ "$dir_gfid_0" != "$dir_gfid_1" ]

#We will choose the first brick as the source of truth
LATEST_DIR_MTIME=$(stat -c %Y $B0/${V0}0/dir2)

TEST $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}0 /dir2
#Lookup should work
TEST ls $M0
TEST ls $M0/dir2

#There will be glusterfs-anonymous-inode, to clear that do a heal
#TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#gfid split-brain should be resolved for dir2
dir_gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/dir2)
TEST [ "$dir_gfid_0" == "$dir_gfid_1" ]

HEALED_DIR_MTIME=$(stat -c %Y $B0/${V0}1/dir2)
TEST [ "$LATEST_DIR_MTIME" == "$HEALED_DIR_MTIME" ]

cleanup;
