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

# simple getfacl setfacl commands
TEST touch testfile
TEST setfacl -m u:14:r testfile
TEST getfacl testfile

cd
TEST umount $N0
cleanup

