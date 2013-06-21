#!/bin/bash

. $(dirname $0)/../include.rc
cleanup

#1
TEST glusterd
TEST pidof glusterd
#3
TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 nfs.drc on
TEST $CLI volume start $V0
sleep 5
TEST mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0 $N0
cd $N0
#7
TEST dbench -t 10 10
TEST rm -rf $N0/*
cd
TEST umount $N0
#10
TEST $CLI volume set $V0 nfs.drc-size 10000
cleanup
