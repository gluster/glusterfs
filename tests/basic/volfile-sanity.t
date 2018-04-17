#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

## Start and create a volume
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0  $H0:$B0/${V0}1

killall glusterd

## Mount FUSE with caching disabled (read-write)
TEST $GFS -f /var/lib/glusterd/vols/${V0}/${V0}.${H0}.*.vol
TEST $GFS -f /var/lib/glusterd/vols/${V0}/${V0}.tcp-fuse.vol $M0

TEST $(df -h $M0 | grep -q ${V0})
TEST $(cat /proc/mounts | grep -q $M0)

TEST ! stat $M0/newfile;
TEST touch $M0/newfile;
TEST rm $M0/newfile;

cleanup;
