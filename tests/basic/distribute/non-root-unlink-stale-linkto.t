#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../common-utils.rc

# Test overview:
# Check that non-privileged users can also clean up stale linkto files
#
# 1. Use the current parallel-readdir behaviour of changing the DHT child subvols
# in the graph to generate stale linkto files
# 2. Access the file with the stale linkto file as a non-root user
# 3. This should now succeed (returned EIO before commit 3fb1df7870e03c9de)

USERNAME=user11
cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0-{1,2}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 performance.parallel-readdir on

# Mount using FUSE and create a file
TEST glusterfs -s $H0 --volfile-id $V0 $M0

# Create a file for testing
TEST dd if=/dev/urandom of=$M0/FILE-1 count=1 bs=16k

#Rename to create a linkto file
TEST mv $M0/FILE-1 $M0/FILE-2

# This should change the graph and cause the linkto values to become stale
TEST $CLI volume set $V0 performance.parallel-readdir off

$CLI volume set $V0 allow-insecure on


TEST useradd -m $USERNAME

#Use a fresh mount so as to trigger a lookup everywhere
TEST glusterfs -s $H0 --volfile-id $V0 $M1
TEST run_cmd_as_user $USERNAME "ls $M1/FILE-2"


# Cleanup
TEST userdel --force $USERNAME
cleanup

