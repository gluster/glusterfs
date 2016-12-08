#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd

#Create arbiter volume.
TEST $CLI volume create $V0 replica 3 arbiter 1  $H0:$B0/${V0}{0,1,2}
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#syntax check for remove-brick.
TEST ! $CLI volume remove-brick $V0 replica 2  $H0:$B0/${V0}0 force
TEST ! $CLI volume remove-brick $V0 replica 2  $H0:$B0/${V0}1 force

#convert to replica 2 volume
TEST $CLI volume remove-brick $V0 replica 2  $H0:$B0/${V0}2 force
EXPECT "1 x 2 = 2" volinfo_field $V0 "Number of Bricks"

TEST mkdir $M0/dir
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=1024
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}1 | sort)

#Mount serves the correct file size
EXPECT "1048576" stat -c %s $M0/file

#Check file size in bricks
EXPECT "1048576" stat -c %s $B0/${V0}0/file
EXPECT "1048576" stat -c %s $B0/${V0}1/file

TEST force_umount $M0
cleanup;
