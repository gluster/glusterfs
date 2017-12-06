#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../common-utils.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 16MB
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST dd if=/dev/zero conv=fsync of=$M0/foo bs=1M count=100

#This should ensure /.shard is created on the bricks.
TEST stat $B0/${V0}0/.shard

gfid_foo=$(get_gfid_string $M0/foo)

TEST stat $B0/${V0}0/.shard/$gfid_foo.1
TEST stat $B0/${V0}0/.shard/$gfid_foo.2
TEST stat $B0/${V0}0/.shard/$gfid_foo.3
TEST stat $B0/${V0}0/.shard/$gfid_foo.4
TEST stat $B0/${V0}0/.shard/$gfid_foo.5
TEST stat $B0/${V0}0/.shard/$gfid_foo.6

# For a file with 7 shards, there should be 7 fsyncs on the brick. Without this
# fix, I was seeing only 1 fsync (on the base shard alone).

EXPECT "7" echo `$CLI volume profile $V0 info incremental | grep -w FSYNC | awk '{print $8}'`

useradd -M test_user 2>/dev/null

TEST touch $M0/bar

# Change ownership to non-root on bar.
TEST chown test_user:test_user $M0/bar

TEST $CLI volume profile $V0 stop
TEST $CLI volume profile $V0 start

# Write 100M of data on bar as non-root.
TEST run_cmd_as_user test_user "dd if=/dev/zero conv=fsync of=$M0/bar bs=1M count=100"

EXPECT "7" echo `$CLI volume profile $V0 info incremental | grep -w FSYNC | awk '{print $8}'`

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
userdel test_user
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
