#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc
cleanup;

TEST glusterd
TEST pidof glusterd

## Create a volume and set auth.allow using cidr format ip

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 auth.allow 127.0.0.1/20
TEST $CLI volume start $V0


## mount the volume and create a file on the mount point

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0
TEST touch $M0/tmp1

## Stop the volume and do the cleanup

TEST $CLI volume stop $V0
cleanup
