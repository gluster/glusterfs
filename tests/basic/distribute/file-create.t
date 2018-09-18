#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../common-utils.rc
. $(dirname $0)/../../dht.rc

# Test overview: Test file creation in various scenarios


# Test 1 : "dht.file.hashed-subvol.<filename>"
# Get the hashed subvolume for file1 in dir1 using xattr
# Create file1 in dir1
# Check if the file is created in the brick returned by xattr

hashdebugxattr="dht.file.hashed-subvol."

cleanup

TEST glusterd
TEST pidof glusterd

# We want fixed size bricks to test min-free-disk

# Create 2 loop devices, one per brick.
TEST   truncate -s 25M $B0/brick1
TEST   truncate -s 25M $B0/brick2

TEST   L1=`SETUP_LOOP $B0/brick1`
TEST   MKFS_LOOP $L1

TEST   L2=`SETUP_LOOP $B0/brick2`
TEST   MKFS_LOOP $L2


TEST   mkdir -p $B0/${V0}{1,2}

TEST   MOUNT_LOOP $L1 $B0/${V0}1
TEST   MOUNT_LOOP $L2 $B0/${V0}2


# Create a plain distribute volume with 2 subvols.
TEST   $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST   $CLI volume start $V0;
EXPECT "Started" volinfo_field $V0 'Status';

TEST   $CLI volume set $V0 cluster.min-free-disk 40%
#TEST   $CLI volume set $V0 client-log-level DEBUG

# Mount using FUSE and create a file
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST mkdir $M0/dir1

######################################################
# Test 1 : Test file creation on correct hashed subvol
######################################################

hashed="$V0-client-0"
TEST dht_first_filename_with_hashsubvol "$hashed" $M0/dir1 "big-"
firstfile=$fn_return_val

#Create a large file to fill up $hashed past the min-free-disk limits
TEST  dd if=/dev/zero of=$M0/dir1/$firstfile bs=1M count=15

brickpath_0=$(cat "$M0/.meta/graphs/active/$hashed/options/remote-subvolume")
brickpath_1=$(cat "$M0/.meta/graphs/active/$V0-client-1/options/remote-subvolume")

TEST stat "$brickpath_0/dir1/$firstfile"
EXPECT "0" is_dht_linkfile "$brickpath_0/dir1/$firstfile"


######################################################
# Test 2: Create a file which hashes to the subvol which has crossed
# the min-free-disk limit. It should be created on the other subvol
######################################################

# DHT only checks disk usage every second. Create a new file and introduce a
# delay here to ensure DHT updates the in memory disk usage
sleep 2
TEST  dd if=/dev/zero of=$M0/dir1/file-2 bs=1024 count=1

# Find a file that will hash to $hash_subvol
TEST dht_first_filename_with_hashsubvol $hashed $M0/dir1 "newfile-"
newfile=$fn_return_val
echo $newfile

# Create $newfile - it should be created on the other subvol as its hash subvol
# has crossed the min-free-disk limit
TEST  dd if=/dev/zero of=$M0/dir1/$newfile bs=1024 count=20
TEST stat "$brickpath_0/dir1/$newfile"
EXPECT "1" is_dht_linkfile "$brickpath_0/dir1/$newfile"


#TEST rm -rf $M0/dir1/$firstfile
#TEST rm -rf $M0/dir1/$newfile


######################################################
# Test 3: Test dht_filter_loc_subvol_key
######################################################

TEST dht_first_filename_with_hashsubvol $V0-client-1 $M0/dir1 "filter-"
newfile=$fn_return_val
echo $newfile
TEST dd if=/dev/zero of="$M0/dir1/$newfile@$V0-dht:$hashed" bs=1024 count=20
TEST stat $M0/dir1/$newfile
TEST stat "$brickpath_0/dir1/$newfile"
EXPECT "1" is_dht_linkfile "$brickpath_1/dir1/$newfile"


force_umount $M0
TEST $CLI volume stop $V0
UMOUNT_LOOP ${B0}/${V0}{1,2}
rm -f ${B0}/brick{1,2}


# Cleanup
cleanup

