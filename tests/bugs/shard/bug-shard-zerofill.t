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

gfid_foo=$(get_gfid_string $M0/foo)

TEST build_tester $(dirname $0)/bug-shard-zerofill.c -lgfapi -Wall -O2
TEST $(dirname $0)/bug-shard-zerofill $H0 $V0 /foo `gluster --print-logdir`/glfs-$V0.log

# This should ensure /.shard is created on the bricks.
TEST stat $B0/${V0}0/.shard
TEST stat $B0/${V0}1/.shard
TEST stat $B0/${V0}2/.shard
TEST stat $B0/${V0}3/.shard

EXPECT "4194304" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %s`
EXPECT "2097152" echo `find $B0 -name $gfid_foo.2 | xargs stat -c %s`

EXPECT "1" file_all_zeroes $M0/foo

TEST `echo "abc" >> $M0/foo`

EXPECT_NOT "1" file_all_zeroes $M0/foo

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

TEST rm -f $(dirname $0)/bug-shard-zerofill

cleanup
