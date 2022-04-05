#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume start $V0;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

TEST mkdir $M1/test1;
TEST mkdir $M1/test2;

# User from regular mount can't set namespace, but only the special pid (ie, <0)
TEST ! setfattr -n trusted.glusterfs.namespace -v true $M1/test2;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 --client-pid=-10 --process-name=namespace-test $M2;
#TEST $GFS -s $H0 --volfile-id $V0 --process-name=namespace-test $M2;

TEST df -h $M1

sleep 1;

TEST setfattr -n trusted.glusterfs.namespace -v true $M2/test1;
TEST setfattr -n trusted.glusterfs.namespace -v true $M2/test2;


TEST touch $M1/test2/file{1,2,11,12};
TEST touch $M1/test1/file{1,2,11,12};

# This technically fails if we can only check rename(2).
mv $M1/test1/file1 $M1/test2/file3 ;
TEST grep 'cross-device' $(glusterfsd --print-logdir)/bricks/*

TEST mv $M1/test2/file2 $M1/test2/file4 ;


TEST ! ln $M1/test1/file11 $M1/test2/file5 ;
TEST ln $M1/test1/file2 $M1/test1/file6 ;

TEST kill_brick $V0 $H0 $B0/${V0};

TEST $CLI volume start $V0 force;

sleep 3;

TEST ! ln $M2/test1/file12 $M2/test2/file7 ;
TEST ln $M2/test1/file2 $M2/test1/file8  ;

TEST $CLI volume stop $V0;
TEST $CLI volume start $V0;

sleep 3;
mv $M1/test2/file11 $M1/test1/file9 ;
echo "$(grep 'cross-device' $(glusterfsd --print-logdir)/bricks/* | grep server_rename_resume)"

TEST mv $M1/test2/file12 $M1/test2/file10 ;


TEST ! setfattr -x trusted.glusterfs.namespace $M2/test1;

cleanup;
