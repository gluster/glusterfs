#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off

##### Healing with favorite-child-policy = mtime ######
#####           and self-heal-daemon             ######

TEST $CLI volume set $V0 favorite-child-policy mtime
TEST kill_brick $V0 $H0 $B0/${V0}0
echo "Sink based on mtime" > $M0/f1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Source based on mtime" > $M0/f1

#Gfids of file f1 on bricks 0 & 1 should differ
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f1)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f1)
TEST [ "$gfid_0" != "$gfid_1" ]

TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

#We know that first brick has the latest mtime
LATEST_MTIME_MD5=$(md5sum $B0/${V0}0/f1 | cut -d\  -f1)

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#gfid split-brain should be resolved
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f1)
TEST [ "$gfid_0" == "$gfid_1" ]

HEALED_MD5=$(md5sum $B0/${V0}1/f1 | cut -d\  -f1)
TEST [ "$LATEST_MTIME_MD5" == "$HEALED_MD5" ]

TEST $CLI volume set $V0 self-heal-daemon off


##### Healing with favorite-child-policy = ctime ######
#####            and self-heal-daemon            ######

#gfid split-brain resolution should work even when the granular-enrty-heal is
#enabled
TEST $CLI volume heal $V0 granular-entry-heal enable

TEST $CLI volume set $V0 favorite-child-policy ctime
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Sink based on ctime" > $M0/f2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}0
echo "Source based on ctime" > $M0/f2

#Gfids of file f2 on bricks 0 & 1 should differ
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f2)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f2)
TEST [ "$gfid_0" != "$gfid_1" ]

TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

#We know that second brick has the latest ctime
LATEST_CTIME_MD5=$(md5sum $B0/${V0}1/f2 | cut -d\  -f1)

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#gfid split-brain should be resolved
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f2)
TEST [ "$gfid_0" == "$gfid_1" ]

HEALED_MD5=$(md5sum $B0/${V0}0/f2 | cut -d\  -f1)
TEST [ "$LATEST_CTIME_MD5" == "$HEALED_MD5" ]


#Add one more brick, and heal.
TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST $CLI volume set $V0 self-heal-daemon off


##### Healing using favorite-child-policy = size #####
#####             and client side heal           #####

TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on

#Set the quorum-type to none, and create a gfid split brain
TEST $CLI volume set $V0 cluster.quorum-type none
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Smallest file" > $M0/f3

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
echo "Second smallest file" > $M0/f3

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}2
echo "Biggest among the three files" > $M0/f3

#Bring back the down bricks.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

#Gfids of file f3 on all the bricks should differ
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f3)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f3)
gfid_2=$(gf_get_gfid_xattr $B0/${V0}2/f3)
TEST [ "$gfid_0" != "$gfid_1" ]
TEST [ "$gfid_0" != "$gfid_2" ]
TEST [ "$gfid_1" != "$gfid_2" ]

#We know that second brick has the bigger size file
BIGGER_FILE_MD5=$(md5sum $B0/${V0}1/f3 | cut -d\  -f1)

TEST ls $M0/f3
TEST cat $M0/f3
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#gfid split-brain should be resolved
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f3)
gfid_2=$(gf_get_gfid_xattr $B0/${V0}2/f3)
TEST [ "$gfid_0" == "$gfid_1" ]
TEST [ "$gfid_2" == "$gfid_1" ]

HEALED_MD5_1=$(md5sum $B0/${V0}0/f3 | cut -d\  -f1)
HEALED_MD5_2=$(md5sum $B0/${V0}2/f3 | cut -d\  -f1)
TEST [ "$BIGGER_FILE_MD5" == "$HEALED_MD5_1" ]
TEST [ "$BIGGER_FILE_MD5" == "$HEALED_MD5_2" ]


##### Healing using favorite-child-policy = majority #####
#####             and client side heal               #####

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Does not agree with bricks 0 & 1" > $M0/f4

TEST $CLI v start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick  $V0 $H0 $B0/${V0}2
echo "Agree on bricks 0 & 1" > $M0/f4

#Gfids of file f4 on bricks 0 & 1 should be same and bricks 0 & 2 should differ
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f4)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f4)
gfid_2=$(gf_get_gfid_xattr $B0/${V0}2/f4)
TEST [ "$gfid_0" == "$gfid_1" ]
TEST [ "$gfid_0" != "$gfid_2" ]

#We know that first and second bricks agree with each other. Pick any one of
#them as source
MAJORITY_MD5=$(md5sum $B0/${V0}0/f4 | cut -d\  -f1)

#Bring back the down brick and heal.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST ls $M0/f4
TEST cat $M0/f4
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#gfid split-brain should be resolved
gfid_2=$(gf_get_gfid_xattr $B0/${V0}2/f4)
TEST [ "$gfid_0" == "$gfid_2" ]

HEALED_MD5=$(md5sum $B0/${V0}2/f4 | cut -d\  -f1)
TEST [ "$MAJORITY_MD5" == "$HEALED_MD5" ]

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=1501390
