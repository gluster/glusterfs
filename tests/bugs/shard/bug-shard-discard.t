#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Create a file.
TEST touch $M0/foo
TEST dd if=/dev/urandom of=$M0/foo bs=1M count=10

# This should ensure /.shard is created on the bricks.
TEST stat $B0/${V0}0/.shard
TEST stat $B0/${V0}1/.shard
TEST stat $B0/${V0}2/.shard
TEST stat $B0/${V0}3/.shard

#Note the size of the file, it should be 10M
EXPECT '10485760' stat -c %s $M0/foo

gfid_foo=$(get_gfid_string $M0/foo)

TEST build_tester $(dirname $0)/bug-shard-discard.c -lgfapi -Wall -O2
#Call discard on the file at off=7M and len=3M
TEST $(dirname $0)/bug-shard-discard $H0 $V0 /foo 7340032 3145728 `gluster --print-logdir`/glfs-$V0.log

#Ensure that discard doesn't change the original size of the file.
EXPECT '10485760' stat -c %s $M0/foo

# Ensure that the last shard is all zero'd out
EXPECT "1" file_all_zeroes `find $B0 -name $gfid_foo.2`
EXPECT_NOT "1" file_all_zeroes `find $B0 -name $gfid_foo.1`

# Now unlink the file. And ensure that all shards associated with the file are cleaned up
TEST unlink $M0/foo
TEST ! stat $B0/${V0}0/.shard/$gfid_foo.1
TEST ! stat $B0/${V0}1/.shard/$gfid_foo.1
TEST ! stat $B0/${V0}2/.shard/$gfid_foo.1
TEST ! stat $B0/${V0}3/.shard/$gfid_foo.1
TEST ! stat $B0/${V0}0/.shard/$gfid_foo.2
TEST ! stat $B0/${V0}1/.shard/$gfid_foo.2
TEST ! stat $B0/${V0}2/.shard/$gfid_foo.2
TEST ! stat $B0/${V0}3/.shard/$gfid_foo.2
TEST ! stat $M0/foo

#clean up everything
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

TEST rm -f $(dirname $0)/bug-shard-discard

cleanup
