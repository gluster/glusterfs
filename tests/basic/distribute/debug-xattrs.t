#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../common-utils.rc

# Test overview: Test the virtual xattrs dht provides for debugging

# Test 1 : "dht.file.hashed-subvol.<filename>"
# Get the hashed subvolume for file1 in dir1 using xattr
# Create file1 in dir1
# Check if the file is created in the brick returned by xattr

hashdebugxattr="dht.file.hashed-subvol."

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0-{0..3}
TEST $CLI volume start $V0

# Mount using FUSE and create a file
TEST glusterfs -s $H0 --volfile-id $V0 $M0

# Test 1 : "dht.file.hashed-subvol.<filename>"
# Get the hashed subvolume for file1 in dir1 using xattr
# Create file1 in dir1
# Check if the file is created in the brick returned by xattr
# Create a directory on $M0

TEST mkdir $M0/dir1

xattrname=$hashdebugxattr"file1"

hashed=$(getfattr --only-values -n "$xattrname" $M0/dir1)

# Get the brick path for $hashed
brickpath=$(cat "$M0/.meta/graphs/active/$hashed/options/remote-subvolume")
brickpath=$brickpath"/dir1/file1"

# Create the file for which we checked the xattr
TEST touch $M0/dir1/file1
TEST stat $brickpath

# Non-existent directory
TEST ! getfattr --only-values -n "$xattrname" $M0/dir2


# Cleanup
cleanup

