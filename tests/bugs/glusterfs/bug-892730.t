#!/bin/bash
#
# Bug 892730 - Verify that afr handles EIO errors from the brick properly.
#
# The associated bug describes a problem where EIO errors returned from the
# local filesystem of a brick that is part of a replica volume are exposed to
# the user. This test simulates such failures and verifies that the volume
# operates as expected.
#
########

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST mkdir -p $B0/test{1,2}

# The graph is a two brick replica with error-gen enabled on the second brick
# and configured to return EIO lookup errors 100% of the time. This simulates
# a brick with a crashed or shut down local filesystem. Note that the order in
# which errors occur is a factor in reproducing the original bug (error-gen
# must be enabled in the second brick for this test to be effective).

cat > $B0/test.vol <<EOF
volume test-posix-0
    type storage/posix
    option directory $B0/test1
end-volume

volume test-locks-0
    type features/locks
    subvolumes test-posix-0
end-volume

volume test-posix-1
    type storage/posix
    option directory $B0/test2
end-volume

volume test-error-1
    type debug/error-gen
    option failure 100
    option enable lookup
    option error-no EIO
    subvolumes test-posix-1
end-volume

volume test-locks-1
    type features/locks
    subvolumes test-error-1
end-volume

volume test-replicate-0
    type cluster/replicate
    option background-self-heal-count 0
    subvolumes test-locks-0 test-locks-1
end-volume
EOF

TEST glusterd

TEST glusterfs --volfile=$B0/test.vol --attribute-timeout=0 --entry-timeout=0 $M0

# We should be able to create and remove a file without interference from the
# "broken" brick.

TEST touch $M0/file
TEST rm $M0/file

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

rm -f $B0/test.vol
rm -rf $B0/test1 $B0/test2

cleanup;

