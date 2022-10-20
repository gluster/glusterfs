#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST mkdir -p $B0/1/.glusterfs/vols/
TEST cp $(dirname $0)/brick.vol $B0/
TEST sed -i -e "s#BRICK#${B0}/1#g" $B0/brick.vol

TEST glusterfsd -f $B0/brick.vol -LDEBUG

TEST cp $(dirname $0)/client.vol $B0/1/.glusterfs/vols/
TEST ! glusterfs -s localhost:24011 --volfile-id client $M0

mount_result=$(mount | grep fuse.glusterfs | grep -q "$M0 " && echo True || echo False)

TEST [ $mount_result == "False" ]

TEST sed -i -e 's/#option/option/g'  $B0/brick.vol

TEST pkill -HUP glusterfsd

# give 1 second for graph switch and other such things to happen
sleep 1

TEST glusterfs -s localhost:24011 --volfile-id client -l/tmp/volspec.log $M0

mount_result=$(mount | grep fuse.glusterfs | grep -q "$M0 " && echo True || echo False)

TEST [ $mount_result == "True" ]
#sleep 1
TEST pkill -HUP glusterfsd

# allow time to get event
sleep 1

volfile_changed=$(grep -q "Volume file changed" /tmp/volspec.log && echo True || echo False)
TEST [ $volfile_changed == "True" ]

# Grep in logs to find 'Volfile changed' message

cleanup
