#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../traps.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 nfs.disable false

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs -s $H0 --volfile-id=$V0 $M0

#kill one of the brick process
TEST kill_brick $V0 $H0 $B0/${V0}2

cleanup_user_group () {
	userdel --force dev
	groupdel QA
}
push_trapfunc cleanup_user_group

#create a user and group
TEST useradd dev
TEST groupadd QA

#create a new directory now with special user, group and mode bits
mkdir -m 7777 $M0/dironedown
TEST chown dev $M0/dironedown
TEST chgrp QA $M0/dironedown

#store the permissions for comparision
permission_onedown=`ls -l $M0 | grep dironedown | awk '{print $1}'`

#Now bring up the brick process
TEST $CLI volume start $V0 force

#The updation of directory attrs happens on the revalidate path. Hence, atmax on
#2 lookups the update will happen.
sleep 5
TEST ls $M0/dironedown;

#check directory that was created post brick going down
TEST brick_perm=`ls -l $B0/${V0}2 | grep dironedown | awk '{print $1}'`
TEST echo $brick_perm;
TEST [ ${brick_perm} = ${permission_onedown} ]
uid=`ls -l $B0/${V0}2 | grep dironedown | awk '{print $3}'`
TEST echo $uid
TEST [ $uid = dev ]
gid=`ls -l $B0/${V0}2 | grep dironedown | awk '{print $4}'`
TEST echo $gid
TEST [ $gid = QA ]

cleanup
