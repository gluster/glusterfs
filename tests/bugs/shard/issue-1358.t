#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

FILE_COUNT_TIME=5

function get_file_count {
    ls $1* | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST dd if=/dev/urandom of=$M0/dir/foo bs=4M count=5
gfid_new=$(get_gfid_string $M0/dir/foo)

# Ensure its shards dir is created now.
TEST stat $B0/${V0}0/.shard/$gfid_new.1
TEST stat $B0/${V0}1/.shard/$gfid_new.1
TEST stat $B0/${V0}0/.shard/$gfid_new.2
TEST stat $B0/${V0}1/.shard/$gfid_new.2

# Open a file and store descriptor in fd = 5
exec 5>$M0/dir/foo

# Write something on the file using the open fd = 5
echo "issue-1358" >&5

# Write on the descriptor should be succesful
EXPECT 0 echo $?

# Unlink the same file which is opened in prev step
TEST unlink $M0/dir/foo

# Check the base file
TEST ! stat $M0/dir/foo
TEST ! stat $B0/${V0}0/foo
TEST ! stat $B0/${V0}1/foo

# Write something on the file using the open fd = 5
echo "issue-1281" >&5

# Write on the descriptor should be succesful
EXPECT 0 echo $?

# Check ".shard/.remove_me"
EXPECT_WITHIN $FILE_COUNT_TIME 1 get_file_count $B0/${V0}0/.shard/.remove_me/$gfid_new
EXPECT_WITHIN $FILE_COUNT_TIME 1 get_file_count $B0/${V0}1/.shard/.remove_me/$gfid_new

# Close the fd = 5
exec 5>&-

###### To see the shards deleted, wait for 10 mins or repeat the same steps i.e open a file #####
###### write something to it, unlink it and close it. This will wake up the thread that is ######
###### responsible to delete the shards

TEST touch $M0/dir/new
exec 6>$M0/dir/new
echo "issue-1358" >&6
EXPECT 0 echo $?
TEST unlink $M0/dir/new
exec 6>&-

# Now check the ".shard/remove_me" and the gfid will not be there
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/.remove_me/$gfid_new
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}1/.shard/.remove_me/$gfid_new

# check for the absence of shards
TEST ! stat $B0/${V0}0/.shard/$gfid_new.1
TEST ! stat $B0/${V0}1/.shard/$gfid_new.1
TEST ! stat $B0/${V0}0/.shard/$gfid_new.2
TEST ! stat $B0/${V0}1/.shard/$gfid_new.2

#### Create the file with same name and check creation and deletion works fine ######
TEST dd if=/dev/urandom of=$M0/dir/foo bs=4M count=5
gfid_new=$(get_gfid_string $M0/dir/foo)

# Ensure its shards dir is created now.
TEST stat $B0/${V0}0/.shard/$gfid_new.1
TEST stat $B0/${V0}1/.shard/$gfid_new.1
TEST stat $B0/${V0}0/.shard/$gfid_new.2
TEST stat $B0/${V0}1/.shard/$gfid_new.2

TEST unlink $M0/dir/foo
cleanup

