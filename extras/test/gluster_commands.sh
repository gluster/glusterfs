#!/bin/bash


#  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.


# This script tests the basics gluster cli commands.

echo "Starting glusterd"
glusterd
if [ $? -ne 0 ]; then
    echo "Could not start glusterd.Exiting"
    exit;
else
    echo "glusterd started"
fi

if [ ! -d "/exports" ]; then
    mkdir /exports;
    mkdir /exports/exp{1..10};
else
    mkdir /exports/exp{1..10};
fi

if [ ! -d "/mnt/client" ]; then
    mkdir /mnt/client -p;
fi


set -e #exit at the first error that happens

# create distribute volume and try start, mount, add-brick, replace-brick, remove-brick, stop, unmount, delete

echo "Creating distribute volume........"
gluster volume create vol `hostname`:/exports/exp1
gluster volume info

echo "Starting distribute volume........"
gluster volume start vol
gluster volume info
sleep 1
mount -t glusterfs `hostname`:vol /mnt/client
sleep 1
df -h

echo "adding-brick......."
gluster volume add-brick vol `hostname`:/exports/exp2
gluster volume info
sleep 1
umount /mnt/client
mount -t glusterfs `hostname`:vol /mnt/client
df -h
sleep 1

echo "replacing brick......"
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 status
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 pause
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 status
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 status
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 commit


echo "replcing brick for abort operation"
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 status
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 pause
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 status
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 status
gluster volume replace-brick vol `hostname`:/exports/exp3 `hostname`:/exports/exp1 abort


gluster volume info
sleep 1
df -h
sleep 1

echo "removing brick......."
gluster --mode=script volume remove-brick vol `hostname`:/exports/exp2
gluster volume info
sleep 1
df -h
sleep 1

echo "stopping distribute volume......"
gluster --mode=script volume stop vol
gluster volume info
sleep 1
umount /mnt/client
df -h

echo "deleting distribute volume......"
gluster --mode=script volume delete vol
gluster volume info
sleep 1

# create replicate volume and try start, mount, add-brick, replace-brick, remove-brick, stop, unmount, delete
echo "creating replicate volume......"
gluster volume create mirror replica 2 `hostname`:/exports/exp1 `hostname`:/exports/exp2
gluster volume info
sleep 1

echo "starting replicate volume......"
gluster volume start mirror
gluster volume info
sleep 1
mount -t glusterfs `hostname`:mirror /mnt/client
sleep 1
df -h
sleep 1

echo "adding-brick......."
gluster volume add-brick mirror `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1
df -h
sleep 1

echo "replacing-brick....."
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 pause
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 commit
gluster volume info
sleep 1
df -h
sleep 1

echo "replacing brick for abort operation"
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 status
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 pause
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 status
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 status
gluster volume replace-brick mirror `hostname`:/exports/exp5 `hostname`:/exports/exp1 abort

gluster volume info
sleep 1
df -h
sleep 1

echo "removeing-brick....."
gluster --mode=script volume remove-brick mirror `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1
df -h
sleep 1

echo "stopping replicate volume....."
gluster --mode=script volume stop mirror
gluster volume info
sleep 1
umount /mnt/client
df -h

echo "deleting replicate volume....."
gluster --mode=script volume delete mirror
gluster volume info
sleep 1

# create stripe volume and try start, mount, add-brick, replace-brick, remove-brick, stop, unmount, delete

echo "creating stripe volume....."
gluster volume create str stripe 2 `hostname`:/exports/exp1 `hostname`:/exports/exp2
gluster volume info
sleep 1

echo "starting stripe volume....."
gluster volume start str
gluster volume info
sleep 1
mount -t glusterfs `hostname`:str /mnt/client
sleep 1
df -h
sleep 1

echo "adding brick...."
gluster volume add-brick str `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1
df -h
sleep 1

echo "replacing brick....."
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 pause
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 commit

gluster volume info
sleep 1
df -h
sleep 1

echo "replacing brick for abort operation"
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 status
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 pause
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 status
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 start
#sleep for 5 seconds
sleep 5
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 status
gluster volume replace-brick str `hostname`:/exports/exp5 `hostname`:/exports/exp1 abort

gluster volume info
sleep 1
df -h
sleep 1

echo "removing-brick....."
gluster --mode=script volume remove-brick str `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1
df -h
sleep 1

echo "stopping stripe volume....."
gluster --mode=script volume stop str
gluster volume info
sleep 1
umount /mnt/client
df -h

echo "deleting stripe volume....."
gluster --mode=script volume delete str
gluster volume info

