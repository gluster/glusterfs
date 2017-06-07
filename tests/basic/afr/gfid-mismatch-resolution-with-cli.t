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
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
cd $M0

##### Healing from latest mtime ######

TEST kill_brick $V0 $H0 $B0/${V0}0
echo "Sink based on mtime" > f1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Source based on mtime" > f1

gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/f1)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f1)
TEST [ "$gfid_0" != "$gfid_1" ]

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

#We know that first brick has the latest mtime
LATEST_MTIME_MD5=$(md5sum $B0/${V0}0/f1 | awk '{print $1}')

TEST $CLI volume heal $V0 split-brain latest-mtime /f1

#gfid split-brain should be resolved
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/f1)
TEST [ "$gfid_0" == "$gfid_1" ]

#Heal the data and check the md5sum
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
HEALED_MD5=$(md5sum $B0/${V0}1/f1 | awk '{print $1}')
TEST [ "$LATEST_MTIME_MD5" == "$HEALED_MD5" ]


##### Healing from bigger file ######

TEST mkdir test
TEST $CLI volume set $V0 self-heal-daemon off
TEST kill_brick $V0 $H0 $B0/${V0}0
echo "Bigger file" > test/f2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "Small file" > test/f2

gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/test/f2)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/test/f2)
TEST [ "$gfid_0" != "$gfid_1" ]

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

#We know that second brick has the bigger file
BIGGER_FILE_MD5=$(md5sum $B0/${V0}1/test/f2 | awk '{print $1}')

TEST $CLI volume heal $V0 split-brain bigger-file /test/f2

#gfid split-brain should be resolved
gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/test/f2)
TEST [ "$gfid_0" == "$gfid_1" ]

#Heal the data and check the md5sum
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
HEALED_MD5=$(md5sum $B0/${V0}0/test/f2 | awk '{print $1}')
TEST [ "$BIGGER_FILE_MD5" == "$HEALED_MD5" ]


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


##### Healing from source brick ######

TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 cluster.quorum-type none
TEST kill_brick $V0 $H0 $B0/${V0}0
echo "We will consider these as sinks" > test/f3
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
echo "We will take this as source" > test/f3

gfid_0=$(gf_get_gfid_xattr $B0/${V0}0/test/f3)
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/test/f3)
gfid_2=$(gf_get_gfid_xattr $B0/${V0}2/test/f3)
TEST [ "$gfid_0" != "$gfid_1" ]
TEST [ "$gfid_1" == "$gfid_2" ]

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

#We will try to heal the split-brain with bigger file option.
#It should fail, since we have same file size in bricks 1 & 2.
EXPECT "No bigger file for file /test/f3" $CLI volume heal $V0 split-brain bigger-file /test/f3

#Now heal from taking the brick 0 as the source
SOURCE_MD5=$(md5sum $B0/${V0}0/test/f3 | awk '{print $1}')

TEST $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}0 /test/f3

#gfid split-brain should be resolved
gfid_1=$(gf_get_gfid_xattr $B0/${V0}1/test/f3)
gfid_2=$(gf_get_gfid_xattr $B0/${V0}2/test/f3)
TEST [ "$gfid_0" == "$gfid_1" ]
TEST [ "$gfid_0" == "$gfid_2" ]

#Heal the data and check the md5sum
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
HEALED_MD5_1=$(md5sum $B0/${V0}1/test/f3 | awk '{print $1}')
HEALED_MD5_2=$(md5sum $B0/${V0}2/test/f3 | awk '{print $1}')
TEST [ "$SOURCE_MD5" == "$HEALED_MD5_1" ]
TEST [ "$SOURCE_MD5" == "$HEALED_MD5_2" ]

cd -
cleanup;
