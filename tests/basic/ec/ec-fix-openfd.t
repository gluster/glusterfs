#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

# This test checks for open fd heal on EC

#Create Volume
cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 performance.read-after-open yes
TEST $CLI volume set $V0 performance.lazy-open no
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 disperse.background-heals 0
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0

#Mount the volume
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

#Touch a file
TEST touch "$M0/test_file"

#Kill a brick
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Open the file in write mode
TEST fd=`fd_available`
TEST fd_open $fd 'rw' "$M0/test_file"

#Bring up the killed brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

#Test the fd count
EXPECT "0" get_fd_count $V0 $H0 $B0/${V0}0 test_file
EXPECT "1" get_fd_count $V0 $H0 $B0/${V0}1 test_file
EXPECT "1" get_fd_count $V0 $H0 $B0/${V0}2 test_file

#Write to file
dd iflag=fullblock if=/dev/urandom bs=1024 count=2 >&$fd 2>/dev/null

#Test the fd count
EXPECT "1" get_fd_count $V0 $H0 $B0/${V0}0 test_file

#Close fd
TEST fd_close $fd

#Stop the volume
TEST $CLI volume stop $V0

#Start the volume
TEST $CLI volume start $V0

#Kill brick1
TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Unmount and mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Calculate md5 sum
md5sum0=`get_md5_sum "$M0/test_file"`

#Bring up the brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

#Kill brick2
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Unmount and mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Calculate md5 sum
md5sum1=`get_md5_sum "$M0/test_file"`

#Bring up the brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

#Kill brick3
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Unmount and mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "2" ec_child_up_count $V0 0

#Calculate md5 sum
md5sum2=`get_md5_sum "$M0/test_file"`

#compare the md5sum
EXPECT "$md5sum0" echo $md5sum1
EXPECT "$md5sum0" echo $md5sum2
EXPECT "$md5sum1" echo $md5sum2

cleanup
