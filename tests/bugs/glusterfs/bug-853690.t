#!/bin/bash
#
# Bug 853690 - Test that short writes do not lead to corruption.
#
# Mismanagement of short writes in AFR leads to corruption and immediately
# detectable split-brain. Write a file to a replica volume using error-gen
# to cause short writes on one replica.
#
# Short writes are also possible during heal. If ignored, the files are marked
# consistent and silently differ. After reading the file, cause a lookup, wait
# for self-heal and verify that the afr xattrs do not match.
#
########

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST mkdir -p $B0/test{1,2}

# Our graph is a two brick replica with 100% frequency of short writes on one
# side of the replica. This guarantees a single write fop leads to an out-of-sync
# situation.
cat > $B0/test.vol <<EOF
volume test-posix-0
    type storage/posix
    option directory $B0/test1
end-volume

volume test-error-0
    type debug/error-gen
    option failure 100
    option enable writev
    option error-no GF_ERROR_SHORT_WRITE
    subvolumes test-posix-0
end-volume

volume test-locks-0
    type features/locks
    subvolumes test-error-0
end-volume

volume test-posix-1
    type storage/posix
    option directory $B0/test2
end-volume

volume test-locks-1
    type features/locks
    subvolumes test-posix-1
end-volume

volume test-replicate-0
    type cluster/replicate
    option background-self-heal-count 0
    subvolumes test-locks-0 test-locks-1
end-volume
EOF

TEST glusterd

TEST glusterfs --volfile=$B0/test.vol --attribute-timeout=0 --entry-timeout=0 $M0

# Send a single write, guaranteed to be short on one replica, and attempt to
# read the data back. Failure to detect the short write results in different
# file sizes and immediate split-brain (EIO).
TEST dd if=/dev/urandom of=$M0/file bs=128k count=1
TEST dd if=$M0/file of=/dev/null bs=128k count=1
########
#
# Test self-heal with short writes...
#
########

# Cause a lookup and wait a few seconds for posterity. This self-heal also fails
# due to a short write.
TEST ls $M0/file
# Verify the attributes on the healthy replica do not reflect consistency with
# the other replica.
xa=`getfattr -n trusted.afr.test-locks-0 -e hex $B0/test2/file 2>&1 | grep = | cut -f2 -d=`
EXPECT_NOT 0x000000000000000000000000 echo $xa

TEST rm -f $M0/file
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

rm -f $B0/test.vol
rm -rf $B0/test1 $B0/test2

cleanup;

