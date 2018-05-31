#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc
cleanup;
#This test case verifies attributes (uid/gid/perm) for the
#directory are healed after stop/start brick. To verify the same
#test case change attributes of the directory after down a DHT subvolume
#and one AFR children. After start the volume with force and run lookup
#operation attributes should be healed on started bricks at the backend.


TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume start $V0
TEST useradd dev -M
TEST groupadd QA

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST mkdir $M0/dironedown

TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "5" online_brick_count

TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "4" online_brick_count

TEST kill_brick $V0 $H0 $B0/${V0}4
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "3" online_brick_count

TEST kill_brick $V0 $H0 $B0/${V0}5
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "2" online_brick_count

TEST chown dev $M0/dironedown
TEST chgrp QA $M0/dironedown
TEST chmod 777 $M0/dironedown

#store the permissions for comparision
permission_onedown=`ls -l $M0 | grep dironedown | awk '{print $1}'`

TEST $CLI volume start $V0 force
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "6" online_brick_count

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

#Run lookup two times to hit revalidate code path in dht
# to heal user attr

TEST ls $M0/dironedown

#check attributes those were created post brick going down
TEST brick_perm=`ls -l $B0/${V0}3 | grep dironedown | awk '{print $1}'`
TEST echo $brick_perm
TEST [ ${brick_perm} = ${permission_onedown} ]
uid=`ls -l $B0/${V0}3 | grep dironedown | awk '{print $3}'`
TEST echo $uid
TEST [ $uid = dev ]
gid=`ls -l $B0/${V0}3 | grep dironedown | awk '{print $4}'`
TEST echo $gid
TEST [ $gid = QA ]

TEST umount $M0
userdel --force dev
groupdel QA

cleanup
exit

