#!/bin/bash
#
# Bug 949242 - Test basic fallocate functionality.
#
# Run several commands to verify basic fallocate functionality. We verify that
# fallocate creates and allocates blocks to a file. We also verify that the keep
# size option does not modify the file size.
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../fallocate.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

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

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
