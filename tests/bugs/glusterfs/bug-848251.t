#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;

TEST $CLI volume start $V0;

#enable quota
TEST $CLI volume quota $V0 enable;

#mount on a random dir
TEST MOUNTDIR="/tmp/$RANDOM"
TEST mkdir $MOUNTDIR
TEST glusterfs -s $H0 --volfile-id=$V0 $MOUNTDIR

function set_quota(){
        mkdir "$MOUNTDIR/$name"
        $CLI volume quota $V0 limit-usage /$name 50KB
}

function quota_list(){
        $CLI volume quota $V0 list | grep -- /$name | awk '{print $3}'
}

TEST   name=":d1"
#file name containing ':' in the start
TEST   set_quota
EXPECT "80%" quota_list

TEST   name=":d1/d:1"
#file name containing ':' in between
TEST   set_quota
EXPECT "80%" quota_list

TEST   name=":d1/d:1/d1:"
#file name containing ':' in the end
TEST   set_quota
EXPECT "80%" quota_list

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $MOUNTDIR
TEST   rm -rf $MOUNTDIR
TEST $CLI volume stop $V0
EXPECT "1" get_aux

cleanup;
