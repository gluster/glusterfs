#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

function pidgrep()
{
    ps ax | grep "$1" | grep -v grep | awk '{print $1}' | head -1
}


## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id=$V0 --acl $M0
MOUNT_PID=`ps ax |grep "glusterfs -s $H0 --volfile-id=$V0 --acl $M0" | grep -v grep | awk '{print $1}' | head -1`

for i in {1..25};
do
    mkdir $M0/tmp_$i && cat /etc/hosts > $M0/tmp_$i/file
    cp -RPp $M0/tmp_$i $M0/newtmp_$i && cat /etc/hosts > $M0/newtmp_$i/newfile
done

EXPECT "$MOUNT_PID" pidgrep $MOUNT_PID
TEST rm -rf $M0/*
umount $M0

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;

cleanup;
