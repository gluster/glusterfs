#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick0 $H0:$B0/brick1
TEST $CLI volume start $V0

sleep 5

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 \
$M0;

TEST touch $M0/file
TEST mkdir $M0/dir

TEST mount -t nfs -o vers=3,nolock $H0:/$V0 $N0
cd $N0

rm -rf * &

TEST mount -t nfs -o retry=0,nolock,vers=3 $H0:/$V0 $N1;

cd;

kill %1;

TEST umount $N0
TEST umount $N1;

cleanup
