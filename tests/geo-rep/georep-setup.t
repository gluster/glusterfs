#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $GMV0 replica 2  $H0:$B0/${GMV0}{1,2,3,4};

TEST $CLI volume start $GMV0

TEST $CLI volume create $GSV0 replica 2  $H0:$B0/${GSV0}{1,2,3,4};

TEST $CLI volume start $GSV0

TEST $CLI system:: execute gsec_create

TEST $CLI volume geo-rep $GMV0 $H0::$GSV0 create push-pem

TEST $CLI volume geo-rep $GMV0 $H0::$GSV0 start

sleep 80 # after start geo-rep takes a minute to get stable

TEST ! "$CLI volume geo-rep $GMV0 $H0::$GSV0 status | egrep -i 'faulty'"

TEST  "$CLI volume geo-rep $GMV0 $H0::$GSV0 status | egrep -i 'Changelog crawl'"

TEST $CLI volume geo-rep $GMV0 $H0::$GSV0 stop

TEST $CLI volume geo-rep $GMV0 $H0::$GSV0 delete
