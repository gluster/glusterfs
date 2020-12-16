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
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 features.shard-creation-rate 2
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

#####################################################################
##### 1. Test fallocate works fine with introduction of creation rate
#####################################################################
TEST fallocate -l 20M $M0/foo
gfid_new=$(get_gfid_string $M0/foo)

# Check for the base shard
TEST stat $M0/foo
TEST stat $B0/${V0}0/foo

# There should be 4 associated shards
EXPECT_WITHIN $FILE_COUNT_TIME 4 get_file_count $B0/${V0}0/.shard/$gfid_new

# Delete the base shard and check shards get cleaned up
TEST unlink $M0/foo

TEST ! stat $M0/foo
TEST ! stat $B0/${V0}0/foo

# There should be no shards now
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/$gfid_new

########################################################################
##### 2. Test create/write works fine with introduction of creation rate
########################################################################
# Create a 9M sharded file
TEST dd if=/dev/zero of=$M0/bar bs=1024 count=9216
gfid_new=$(get_gfid_string $M0/bar)

# Ensure its shards are created.
TEST stat $B0/${V0}0/.shard/$gfid_new.1
TEST stat $B0/${V0}0/.shard/$gfid_new.2

# There should be 2 associated shards
EXPECT_WITHIN $FILE_COUNT_TIME 2 get_file_count $B0/${V0}0/.shard/$gfid_new

# Delete the base shard and check shards get cleaned up
TEST unlink $M0/bar

TEST ! stat $M0/bar
TEST ! stat $B0/${V0}0/bar

# There should be no shards now
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/$gfid_new
cleanup
