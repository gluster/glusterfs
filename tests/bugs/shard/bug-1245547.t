#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
#Create a file.
TEST touch $M0/foo
#Write some data into it.
TEST `echo "abc" > $M0/foo`

#This should ensure /.shard is created on the bricks.
TEST stat $B0/${V0}0/.shard
TEST stat $B0/${V0}1/.shard

#Create a file 'bar' with holes.
TEST touch $M0/bar
TEST truncate -s 10G $M0/bar
#Unlink on such a file should succeed.
TEST unlink $M0/bar
#
#Create a file 'baz' with holes.
TEST touch $M0/baz
TEST truncate -s 10G $M0/baz
#Rename with a sharded existing dest that has holes must succeed.
TEST mv -f $M0/foo $M0/baz

cleanup;
