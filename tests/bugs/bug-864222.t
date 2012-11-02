#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume start $V0

sleep 5

TEST mount -t nfs -o vers=3,nolock $H0:/$V0 $N0
cd $N0

TEST ls

TEST $CLI volume set $V0 nfs.enable-ino32 on
# Main test. This should pass.
TEST ls

cd
TEST umount $N0
cleanup

