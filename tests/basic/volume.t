#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '6' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{9,10,11};
EXPECT '9' brick_count $V0

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}{1,2,3} force;
EXPECT '6' brick_count $V0

TEST $CLI volume top $V0 read-perf bs 4096 count 1000
TEST $CLI volume top $V0 write-perf bs 1048576 count 2

TEST touch $M0/foo

# statedump path should be a directory, setting it to a file path should fail

TEST ! $CLI v set $V0 server.statedump-path $M0/foo;
EXPECT '/var/run/gluster' $CLI v get $V0 server.statedump-path

#set the statedump path to an existing ditectory which should succeed
TEST mkdir $D0/level;
TEST $CLI v set $V0 server.statedump-path $D0/level
EXPECT '/level' volinfo_field $V0 'server.statedump-path'

ret=$(ls $D0/level | wc -l);
TEST [ $ret == 0 ]
TEST $CLI v statedump $V0;
ret=$(ls $D0/level | wc -l);
TEST ! [ $ret == 0 ]

#set the statedump path to a non - existing directory which should fail
TEST ! $CLI v set $V0 server.statedump-path /root/test
EXPECT '/level' volinfo_field $V0 'server.statedump-path'

TEST rm -rf $D0/level

TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status'

TEST $CLI volume delete $V0
TEST ! $CLI volume info $V0

cleanup;
