#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

function get_file_count {
    ls $1* | wc -l
}

FILE_COUNT_TIME=5

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
# Unlink a temporary file to trigger creation of .remove_me
TEST touch $M0/tmp
TEST unlink $M0/tmp

TEST stat $B0/${V0}0/.shard/.remove_me
TEST stat $B0/${V0}1/.shard/.remove_me

TEST dd if=/dev/zero of=$M0/dir/file bs=1024 count=9216
gfid_file=$(get_gfid_string $M0/dir/file)

# Create marker file from the backend to simulate ENODATA.
touch $B0/${V0}0/.shard/.remove_me/$gfid_file
touch $B0/${V0}1/.shard/.remove_me/$gfid_file

# Set block and file size to incorrect values of 64MB and 5MB to simulate "stale xattrs" case
# and confirm that the correct values are set when the actual unlink takes place

TEST setfattr -n trusted.glusterfs.shard.block-size -v 0x0000000004000000 $B0/${V0}0/.shard/.remove_me/$gfid_file
TEST setfattr -n trusted.glusterfs.shard.block-size -v 0x0000000004000000 $B0/${V0}1/.shard/.remove_me/$gfid_file

TEST setfattr -n trusted.glusterfs.shard.file-size -v 0x0000000000500000000000000000000000000000000000000000000000000000 $B0/${V0}0/.shard/.remove_me/$gfid_file
TEST setfattr -n trusted.glusterfs.shard.file-size -v 0x0000000000500000000000000000000000000000000000000000000000000000 $B0/${V0}1/.shard/.remove_me/$gfid_file

# Sleep for 2 seconds to prevent posix_gfid_heal() from believing marker file is "fresh" and failing lookup with ENOENT
sleep 2

TEST unlink $M0/dir/file
TEST ! stat $B0/${V0}0/dir/file
TEST ! stat $B0/${V0}1/dir/file

EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/.remove_me/$gfid_file
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}1/.shard/.remove_me/$gfid_file
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/$gfid_file
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}1/.shard/$gfid_file

##############################
### Repeat test for rename ###
##############################

TEST touch $M0/src
TEST dd if=/dev/zero of=$M0/dir/dst bs=1024 count=9216
gfid_dst=$(get_gfid_string $M0/dir/dst)

# Create marker file from the backend to simulate ENODATA.
touch $B0/${V0}0/.shard/.remove_me/$gfid_dst
touch $B0/${V0}1/.shard/.remove_me/$gfid_dst

# Set block and file size to incorrect values of 64MB and 5MB to simulate "stale xattrs" case
# and confirm that the correct values are set when the actual unlink takes place

TEST setfattr -n trusted.glusterfs.shard.block-size -v 0x0000000004000000 $B0/${V0}0/.shard/.remove_me/$gfid_dst
TEST setfattr -n trusted.glusterfs.shard.block-size -v 0x0000000004000000 $B0/${V0}1/.shard/.remove_me/$gfid_dst

TEST setfattr -n trusted.glusterfs.shard.file-size -v 0x0000000000500000000000000000000000000000000000000000000000000000 $B0/${V0}0/.shard/.remove_me/$gfid_dst
TEST setfattr -n trusted.glusterfs.shard.file-size -v 0x0000000000500000000000000000000000000000000000000000000000000000 $B0/${V0}1/.shard/.remove_me/$gfid_dst

# Sleep for 2 seconds to prevent posix_gfid_heal() from believing marker file is "fresh" and failing lookup with ENOENT
sleep 2

TEST mv -f $M0/src $M0/dir/dst
TEST ! stat $B0/${V0}0/src
TEST ! stat $B0/${V0}1/src

EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/.remove_me/$gfid_dst
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}1/.shard/.remove_me/$gfid_dst
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/$gfid_dst
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}1/.shard/$gfid_dst

cleanup
