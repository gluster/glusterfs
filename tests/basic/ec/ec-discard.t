#!/bin/bash
#
# Test discard functionality
#
# Test that basic discard (hole punch) functionality works via the fallocate
# command line tool. Hole punch deallocates a region of a file, creating a hole
# and a zero-filled data region. We verify that hole punch works, frees blocks
# and that subsequent reads do not read stale data (caches are invalidated).
#
# NOTE: fuse fallocate is known to be broken with regard to cache invalidation
# 	up to 3.9.0 kernels. Therefore, FOPEN_KEEP_CACHE is not used in this
#	test (opens will invalidate the fuse cache).
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../fallocate.rc
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

#Check for fallocate and hole punch support
require_fallocate -l 1m $M0/file
require_fallocate -p -l 512k $M0/file && rm -f $M0/file

#Write some data, punch a hole and verify the file content changes
TEST dd if=/dev/urandom of=$M0/file bs=1024k count=1
TEST cp $M0/file $M0/file.copy.pre
TEST fallocate -p -o 512k -l 128k $M0/file
TEST ! cmp $M0/file.copy.pre $M0/file
TEST rm -f $M0/file $M0/file.copy.pre

#Allocate some blocks, punch a hole and verify block allocation
TEST fallocate -l 1m $M0/file
blksz=`stat -c %B $M0/file`
nblks=`stat -c %b $M0/file`
TEST [ $(($blksz * $nblks)) -ge 1048576 ]
TEST fallocate -p -o 512k -l 128k $M0/file
nblks=`stat -c %b $M0/file`
TEST [ $(($blksz * $nblks)) -lt $((933889)) ]
TEST unlink $M0/file

###Punch hole test cases without fallocate
##With write
#Touching starting boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 0 -l 500 $B0/test_file
TEST fallocate -p -o 0 -l 500 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Touching boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 500 -l 1548 $B0/test_file
TEST fallocate -p -o 500 -l 1548 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Not touching boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 500 -l 1000 $B0/test_file
TEST fallocate -p -o 500 -l 1000 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Over boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 1500 -l 1000 $B0/test_file
TEST fallocate -p -o 1500 -l 1000 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

###Punch hole test cases with fallocate
##Without write

#Zero size
TEST dd if=/dev/urandom of=$M0/test_file bs=1024 count=8
TEST ! fallocate -p -o 1500 -l 0 $M0/test_file

#Negative size
TEST ! fallocate -p -o 1500 -l -100 $M0/test_file
TEST rm -f $M0/test_file

#Touching boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 2048 -l 2048 $B0/test_file
TEST fallocate -p -o 2048 -l 2048 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Touching boundary,multiple stripe
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 2048 -l 4096 $B0/test_file
TEST fallocate -p -o 2048 -l 4096 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

##With write

#Size ends in boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 600 -l 3496 $B0/test_file
TEST fallocate -p -o 600 -l 3496 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Offset at boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 2048 -l 3072 $B0/test_file
TEST fallocate -p -o 2048 -l 3072 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Offset and Size not at boundary covering a stripe
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 1500 -l 3000 $B0/test_file
TEST fallocate -p -o 1500 -l 3000 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file
TEST rm -f $B0/test_file $M0/test_file

#Offset and Size not at boundary
TEST dd if=/dev/urandom of=$B0/test_file bs=1024 count=8
TEST cp $B0/test_file $M0/test_file
TEST fallocate -p -o 1000 -l 3072 $B0/test_file
TEST fallocate -p -o 1000 -l 3072 $M0/test_file
TEST md5_sum=`get_md5_sum $B0/test_file`
EXPECT $md5_sum get_md5_sum $M0/test_file

#Data Corruption Tests
#Kill brick1 and brick2
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#Unmount and mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#verify md5 sum
EXPECT $md5_sum get_md5_sum $M0/test_file

#Bring up the bricks
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#Kill brick3 and brick4
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#Unmount and mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#verify md5 sum
EXPECT $md5_sum get_md5_sum $M0/test_file

#Bring up the bricks
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#Kill brick5 and brick6
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST kill_brick $V0 $H0 $B0/${V0}5
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#Unmount and mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "4" ec_child_up_count $V0 0

#verify md5 sum
EXPECT $md5_sum get_md5_sum $M0/test_file

cleanup
