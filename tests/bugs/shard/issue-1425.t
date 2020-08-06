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
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST fallocate -l 20M $M0/foo
gfid_new=$(get_gfid_string $M0/foo)

# Check for the base shard
TEST stat $M0/foo
TEST stat $B0/${V0}0/foo

# There should be 4 associated shards
EXPECT_WITHIN $FILE_COUNT_TIME 4 get_file_count $B0/${V0}0/.shard/$gfid_new

# There should be 1+4 shards and we expect 4 lookups less than on the build without this patch
EXPECT "21" echo `$CLI volume profile $V0 info incremental | grep -w LOOKUP | awk '{print $8}'`

# Delete the base shard and check shards get cleaned up
TEST unlink $M0/foo

TEST ! stat $M0/foo
TEST ! stat $B0/${V0}0/foo

# There should be no shards now
EXPECT_WITHIN $FILE_COUNT_TIME 0 get_file_count $B0/${V0}0/.shard/$gfid_new
cleanup
