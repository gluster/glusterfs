#!/bin/bash
#
# This test case verifies handling node down scenario with optimistic
# changelog enabled on EC volume.
###

SCRIPT_TIMEOUT=300

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

#cleate and start volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume set $V0 disperse.optimistic-change-log on
TEST $CLI volume start $V0

#Mount the volume
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#Verify that all is good
TEST mkdir $M0/test_dir
TEST touch $M0/test_dir/file
sleep 2
EXPECT_WITHIN $IO_WAIT_TIMEOUT "^$" get_hex_xattr trusted.ec.dirty $B0/${V0}0/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT "^$" get_hex_xattr trusted.ec.dirty $B0/${V0}1/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT "^$" get_hex_xattr trusted.ec.dirty $B0/${V0}2/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT "^$" get_hex_xattr trusted.ec.dirty $B0/${V0}3/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT "^$" get_hex_xattr trusted.ec.dirty $B0/${V0}4/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT "^$" get_hex_xattr trusted.ec.dirty $B0/${V0}5/test_dir

#Touch a file and kill two bricks
TEST touch $M0/test_dir/new_file
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#Dirty should be set on up bricks
EXPECT_WITHIN $IO_WAIT_TIMEOUT  "^00000000000000010000000000000001$" get_hex_xattr trusted.ec.dirty $B0/${V0}2/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT  "^00000000000000010000000000000001$" get_hex_xattr trusted.ec.dirty $B0/${V0}3/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT  "^00000000000000010000000000000001$" get_hex_xattr trusted.ec.dirty $B0/${V0}4/test_dir
EXPECT_WITHIN $IO_WAIT_TIMEOUT  "^00000000000000010000000000000001$" get_hex_xattr trusted.ec.dirty $B0/${V0}5/test_dir

#Bring up the down bricks
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#remove mount point contents
TEST rm -rf $M0"/*" 2>/dev/null

# unmount and remount the volume
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

#Create a tar file
TEST mkdir $M0/test_dir
for i in {1..3000};do
dd if=/dev/urandom of=$M0/test_dir/file-$i bs=1k count=10;
done
tar -cf $M0/test_dir.tar $M0/test_dir/ 2>/dev/null
rm -rf $M0/test_dir/

#Untar the tar file
tar -C $M0 -xf $M0/test_dir.tar 2>/dev/null&

#Kill 1st and 2nd brick
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#Stop untaring
TEST kill %1

#Bring up the down bricks
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#Kill 3rd and 4th brick
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST kill_brick $V0 $H0 $B0/${V0}4
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#remove mount point contents
#this will fail if things are wrong
TEST rm -rf $M0"/*" 2>/dev/null

cleanup
