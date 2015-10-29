#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# Create a file.
TEST touch $M0/foo

gfid_foo=`getfattr -n glusterfs.gfid.string $M0/foo 2>/dev/null \
          | grep glusterfs.gfid.string | cut -d '"' -f 2`

TEST fallocate -l 17M $M0/foo
EXPECT '17825792' stat -c %s $M0/foo

# This should ensure /.shard is created on the bricks.
TEST stat $B0/${V0}0/.shard
TEST stat $B0/${V0}1/.shard
TEST stat $B0/${V0}2/.shard
TEST stat $B0/${V0}3/.shard

EXPECT "4194304" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %s`
EXPECT "4194304" echo `find $B0 -name $gfid_foo.2 | xargs stat -c %s`
EXPECT "4194304" echo `find $B0 -name $gfid_foo.3 | xargs stat -c %s`
EXPECT "1048576" echo `find $B0 -name $gfid_foo.4 | xargs stat -c %s`

TEST fallocate -o 102400 -l 17M $M0/foo
EXPECT '17928192' stat -c %s $M0/foo

EXPECT "4194304" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %s`
EXPECT "4194304" echo `find $B0 -name $gfid_foo.2 | xargs stat -c %s`
EXPECT "4194304" echo `find $B0 -name $gfid_foo.3 | xargs stat -c %s`
EXPECT "1150976" echo `find $B0 -name $gfid_foo.4 | xargs stat -c %s`

TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
