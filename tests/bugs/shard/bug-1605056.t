#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

SHARD_COUNT_TIME=5

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 4MB
TEST $CLI volume set $V0 features.shard-lru-limit 25
TEST $CLI volume set $V0 performance.write-behind off

TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M1

# Perform a write that would cause 25 shards to be created under .shard
TEST dd if=/dev/zero of=$M0/foo bs=1M count=104

# Read the file from $M1, indirectly filling up the lru list.
TEST `cat $M1/foo > /dev/null`
statedump=$(generate_mount_statedump $V0 $M1)
sleep 1
EXPECT "25" echo $(grep "inode-count" $statedump | cut -f2 -d'=' | tail -1)
rm -f $statedump

# Delete foo from $M0.
TEST unlink $M0/foo

# Send stat on foo from $M1 to force $M1 to "forget" inode associated with foo.
# Now the ghost shards associated with "foo" are still in lru list of $M1.
TEST ! stat $M1/foo

# Let's force the ghost shards of "foo" out of lru list by looking up more shards
# through I/O on a file named "bar" from $M1. This should crash if the base inode
# had been destroyed by now.

TEST dd if=/dev/zero of=$M1/bar bs=1M count=104

###############################################
#### Now for some inode ref-leak tests ... ####
###############################################

# Expect there to be 29 active inodes - 26 belonging to "bar", 1 for .shard,
# 1 for .shard/remove_me and 1 for '/'
EXPECT_WITHIN $SHARD_COUNT_TIME `expr 26 + 3` get_mount_active_size_value $V0 $M1

TEST rm -f $M1/bar
EXPECT_WITHIN $SHARD_COUNT_TIME 3 get_mount_active_size_value $V0 $M1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
