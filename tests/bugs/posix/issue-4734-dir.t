#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Test for the directory part of https://github.com/gluster/glusterfs/issues/4734
#
# rmdir() of a directory that has an open descriptor must remove it from
# the namespace while keeping the inode alive for that descriptor: fd-based
# metadata operations (fchmod/futimens/fstat) must keep working and the
# inode must remain resolvable by gfid, exactly like on a local filesystem.
# On the brick the directory is parked in .glusterfs/unlink/ while the fd is
# open, and the parked entry must be cleaned up when the last fd is closed.

TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0;
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST $CC -Wall $(dirname $0)/issue-4734-dir.c -o $(dirname $0)/issue-4734-dir

mkdir -p $M0/dir-issue-4734-dir

GOFILE=/tmp/go-issue-4734-dir
rm -f $GOFILE
UNLINKDIR=$B0/$V0/.glusterfs/unlink

# Run the test program in the background; it parks a directory, verifies
# the fd-based and gfid-based operations, then waits for $GOFILE before
# closing its fd (which must clean up the parked entry).
$(dirname $0)/issue-4734-dir $M0/dir-issue-4734-dir $GOFILE &
pid=$!

# Wait for the program to park the directory: the unlink directory on the
# brick must gain an entry.
parked="";
for i in $(seq 1 30); do
    parked=$(ls -A $UNLINKDIR 2>/dev/null | head -1)
    [ -n "$parked" ] && break;
    sleep 1;
done;
TEST [ -n "$parked" ]
TEST [ -e $UNLINKDIR/$parked ]

# Let the program close its fd and exit.
touch $GOFILE
wait $pid;
TEST [ $? -eq 0 ]

# After the last fd is closed, the parked entry must be gone.
sleep 2;
TEST [ -z "$(ls -A $UNLINKDIR 2>/dev/null)" ]

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

rm -f $GOFILE
cleanup;
