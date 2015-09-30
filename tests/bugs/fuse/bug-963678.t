#!/bin/bash
#
# Bug 963678 - Test discard functionality
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

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

# check for fallocate and hole punch support
require_fallocate -l 1m $M0/file
require_fallocate -p -l 512k $M0/file && rm -f $M0/file

# allocate some blocks, punch a hole and verify block allocation
TEST fallocate -l 1m $M0/file
blksz=`stat -c %B $M0/file`
nblks=`stat -c %b $M0/file`
TEST [ $(($blksz * $nblks)) -ge 1048576 ]
TEST fallocate -p -o 512k -l 128k $M0/file

nblks=`stat -c %b $M0/file`
# allow some room for xattr blocks
TEST [ $(($blksz * $nblks)) -lt $((917504 + 16384)) ]
TEST unlink $M0/file

# write some data, punch a hole and verify the file content changes
TEST dd if=/dev/urandom of=$M0/file bs=1024k count=1
TEST cp $M0/file $M0/file.copy.pre
TEST fallocate -p -o 512k -l 128k $M0/file
TEST cp $M0/file $M0/file.copy.post
TEST ! cmp $M0/file.copy.pre $M0/file.copy.post
TEST unlink $M0/file

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
