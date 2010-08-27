#!/bin/bash

#   Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
#   This file is part of GlusterFS.

#   GlusterFS is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published
#   by the Free Software Foundation; either version 3 of the License,
#   or (at your option) any later version.

#   GlusterFS is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.

#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see
#   <http://www.gnu.org/licenses/>.


# This script tests the basics gluster cli commands.

if [ ! -d "/exports" ]; then
    mkdir /exports;
    mkdir /exports/exp{1..10};
else
    mkdir /exports/exp{1..10};
fi

if [ ! -d "/mnt/client" ]; then
    mkdir /mnt/client -p;
fi


# create distribute volume and try start, mount, add-brick, replace-brick, remove-brick, stop, unmount, delete

gluster volume create vol `hostname`:/exports/exp1
gluster volume info

gluster volume start vol
gluster volume info
sleep 1
mount -t glusterfs `hostname`:vol /mnt/client
sleep 1 
df -h


gluster volume add-brick vol `hostname`:/exports/exp2
gluster volume info
sleep 1 
mount -t glusterfs `hostname`:vol /mnt/client
df -h
sleep 1

gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 start
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 status
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 pause
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 abort
gluster volume replace-brick vol `hostname`:/exports/exp1 `hostname`:/exports/exp3 commit
gluster volume info
sleep 1
df -h
sleep 1

gluster volume remove-brick vol `hostname`:/exports/exp2
gluster volume info
sleep 1 
df -h
sleep 1

gluster volume stop vol
gluster volume info
sleep 1
df -h
umount /mnt/client
df -h

gluster volume delete vol
gluster volume info
sleep 1

# create replicate volume and try start, mount, add-brick, replace-brick, remove-brick, stop, unmount, delete

gluster volume create mirror replica 2 `hostname`:/exports/exp1 `hostname`:/exports/exp2
gluster volume info
sleep 1

gluster volume start mirror
gluster volume info
sleep 1 
mount -t glusterfs `hostname`:mirror /mnt/client
sleep 1
df -h
sleep1

gluster volume add-brick mirror replica 2 `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1 
df -h
sleep 1

gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 start
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 pause
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 abort
gluster volume replace-brick mirror `hostname`:/exports/exp1 `hostname`:/exports/exp5 commit
gluster volume info
sleep 1
df -h
sleep 1

gluster volume remove-brick mirror replica 2 `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1 
df -h
sleep 1

gluster volume stop mirror
gluster volume info
sleep 1 
df -h
umount /mnt/client
df -h

gluster volume delete mirror
gluster volume info
sleep 1

# create stripe volume and try start, mount, add-brick, replace-brick, remove-brick, stop, unmount, delete

gluster volume create str stripe 2 `hostname`:/exports/exp1 `hostname`:/exports/exp2
gluster volume info
sleep 1

gluster volume start str
gluster volume info
sleep 1
mount -t glusterfs `hostname`:str /mnt/client
sleep 1 
df -h
sleep 1

gluster volume add-brick str stripe 2 `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1 
df -h
sleep 1

gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 start
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 status
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 pause
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 abort
gluster volume replace-brick str `hostname`:/exports/exp1 `hostname`:/exports/exp5 commit
gluster volume info
sleep 1
df -h

gluster volume remove-brick str stripe 2 `hostname`:/exports/exp3 `hostname`:/exports/exp4
gluster volume info
sleep 1
df -h
sleep 1

gluster volume stop str
gluster volume info
sleep 1
df -h
sleep 1 
umount /mnt/client
df -h

gluster volume delete str
gluster volume info
