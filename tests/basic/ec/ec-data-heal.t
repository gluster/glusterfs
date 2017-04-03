#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This test checks data corruption after heal while IO is going on

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

############ Start IO ###########
TEST touch $M0/file
#start background IO on file
dd if=/dev/urandom of=$M0/file conv=fdatasync &
iopid=$(echo $!)


############ Kill and start brick0 for heal ###########
brick0=$(ps -p $(get_brick_pid $V0 $H0 $B0/${V0}0) -o args)
WORDTOREMOVE=COMMAND
brick0=${brick0//$WORDTOREMOVE/}
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
#sleep so that data can be written which will be healed later
sleep 10
TEST eval $brick0
##wait for heal info to become 0 and kill IO
EXPECT_WITHIN $IO_HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
kill $iopid
EXPECT_WITHIN $IO_HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############### Check md5sum #########################

## unmount and mount get md5sum after killing brick0

brick0=$(ps -p $(get_brick_pid $V0 $H0 $B0/${V0}0) -o args)
WORDTOREMOVE=COMMAND
brick0=${brick0//$WORDTOREMOVE/}
TEST kill_brick $V0 $H0 $B0/${V0}0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
mdsum0=`md5sum $M0/file | awk '{print $1}'`
TEST eval $brick0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

## unmount and mount get md5sum after killing brick1

brick1=$(ps -p $(get_brick_pid $V0 $H0 $B0/${V0}1) -o args)
WORDTOREMOVE=COMMAND
brick1=${brick1//$WORDTOREMOVE/}
TEST kill_brick $V0 $H0 $B0/${V0}1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
mdsum1=`md5sum $M0/file | awk '{print $1}'`
TEST eval $brick1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

## unmount and mount get md5sum after killing brick2

brick2=$(ps -p $(get_brick_pid $V0 $H0 $B0/${V0}2) -o args)
WORDTOREMOVE=COMMAND
brick2=${brick2//$WORDTOREMOVE/}
TEST kill_brick $V0 $H0 $B0/${V0}2

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0
mdsum2=`md5sum $M0/file | awk '{print $1}'`
TEST eval $brick2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

# compare all the three md5sums
EXPECT "$mdsum0" echo $mdsum1
EXPECT "$mdsum0" echo $mdsum2
EXPECT "$mdsum1" echo $mdsum2

cleanup
