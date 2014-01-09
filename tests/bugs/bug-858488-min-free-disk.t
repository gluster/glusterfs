#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Lets create partitions for bricks
TEST truncate -s 100M $B0/brick1
TEST truncate -s 200M $B0/brick2
TEST LO1=`losetup --find --show $B0/brick1`
TEST mkfs.xfs $LO1
TEST LO2=`losetup --find --show $B0/brick2`
TEST mkfs.xfs $LO2
TEST mkdir -p $B0/${V0}1 $B0/${V0}2
TEST mount -t xfs $LO1 $B0/${V0}1
TEST mount -t xfs $LO2 $B0/${V0}2


## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs -s $H0 --volfile-id=$V0 --acl $M0
## Real test starts here
## ----------------------------------------------------------------------------

MINFREEDISKVALUE=90

## Set min free disk to MINFREEDISKVALUE percent
TEST $CLI volume set $V0 cluster.min-free-disk $MINFREEDISKVALUE

## We need to have file name to brick map based on hash.
## We will use this info in test case 0.
i=1
CONTINUE=2
BRICK1FILE=0
BRICK2FILE=0
while [[ $CONTINUE -ne 0 ]]
do
        dd if=/dev/zero of=$M0/file$i.data bs=1024 count=1024 1>/dev/null 2>&1

        if  [[ -e  $B0/${V0}1/file$i.data &&  $BRICK1FILE = "0" ]]
        then
                BRICK1FILE=file$i.data
                CONTINUE=$CONTINUE-1
        fi

        if [[ -e  $B0/${V0}2/file$i.data &&  $BRICK2FILE = "0" ]]
        then
                BRICK2FILE=file$i.data
                CONTINUE=$CONTINUE-1
        fi

        rm $M0/file$i.data
        let i++
done


## Bring free space on one of the bricks to less than minfree value by
## creating one big file.
dd if=/dev/zero of=$M0/fillonebrick.data bs=1024 count=25600 1>/dev/null 2>&1

#Lets find out where it was created
if [ -f $B0/${V0}1/fillonebrick.data ]
then
        FILETOCREATE=$BRICK1FILE
        OTHERBRICK=$B0/${V0}2
else
        FILETOCREATE=$BRICK2FILE
        OTHERBRICK=$B0/${V0}1
fi

##--------------------------------TEST CASE 0-----------------------------------
## If we try to create a file which should go into full brick as per hash, it
## should go into the other brick instead.

## Before that let us create files just to make gluster refresh the stat
## Using touch so it should not change the disk usage stats
for k in {1..20};
do
        touch $M0/dummyfile$k
done

dd if=/dev/zero of=$M0/$FILETOCREATE bs=1024 count=2048 1>/dev/null 2>&1
TEST [ -e $OTHERBRICK/$FILETOCREATE ]

## Done testing, lets clean up
TEST rm -rf $M0/*

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';
$CLI volume delete $V0;

cleanup;
