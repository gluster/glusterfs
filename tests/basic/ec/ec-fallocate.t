#!/bin/bash
#
# Run several commands to verify basic fallocate functionality. We verify that
# fallocate creates and allocates blocks to a file. We also verify that the keep
# size option does not modify the file size.
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

cleanup

#cleate and start volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume start $V0

#Mount the volume
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

# check for fallocate support before continuing the test
require_fallocate -l 1m -n $M0/file && rm -f $M0/file

# fallocate a file and verify blocks are allocated
TEST fallocate -l 1m $M0/file
blksz=`stat -c %b $M0/file`
nblks=`stat -c %B $M0/file`
TEST [ $(($blksz * $nblks)) -eq 1048576 ]

TEST unlink $M0/file

# truncate a file to a fixed size, fallocate and verify that the size does not
# change
TEST truncate -s 1M $M0/file
TEST fallocate -l 2m -n $M0/file
blksz=`stat -c %b $M0/file`
nblks=`stat -c %B $M0/file`
sz=`stat -c %s $M0/file`
TEST [ $sz -eq 1048576 ]
# Note that gluster currently incorporates a hack to limit the number of blocks
# reported as allocated to the file by the file size. We have allocated beyond the
# file size here. Just check for non-zero allocation to avoid setting a land mine
# for if/when that behavior might change.
TEST [ ! $(($blksz * $nblks)) -eq 0 ]
TEST unlink $M0/file

# write some data, fallocate within and outside the range
# and check for data corruption.
TEST dd if=/dev/urandom of=$M0/file bs=1024k count=1
TEST cp $M0/file $M0/file.copy.pre
TEST fallocate -o 512k -l 128k $M0/file
TEST cp $M0/file $M0/file.copy.post
TEST cmp $M0/file.copy.pre $M0/file.copy.post
TEST fallocate -o 1000k -l 128k $M0/file
TEST cp $M0/file $M0/file.copy.post2
TEST ! cmp $M0/file.copy.pre $M0/file.copy.post2
TEST truncate -s 1M $M0/file.copy.post2
TEST cmp $M0/file.copy.pre $M0/file.copy.post2
TEST unlink $M0/file

#Make sure offset/size are modified so that 3 blocks are allocated
TEST touch $M0/f1
TEST fallocate -o 1280 -l 1024 $M0/f1
EXPECT "^2304$" stat -c "%s" $M0/f1
EXPECT "^1536$" stat -c "%s" $B0/${V0}0/f1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup;
