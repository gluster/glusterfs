#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup

TEST verify_lvm_version
TEST glusterd
TEST pidof glusterd
TEST init_n_bricks 3
TEST setup_lvm 3

TEST $CLI volume create $V0 replica 3 $H0:$L{1,2,3}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST $CLI volume set $V0 storage.reserve-size 10MB

#No effect as priority to reserve-size
TEST $CLI volume set $V0 storage.reserve 20

TEST dd if=/dev/zero of=$M0/a bs=100M count=1
sleep 5

#Below dd confirms posix is giving priority to reserve-size
TEST dd if=/dev/zero of=$M0/b bs=40M count=1

sleep 5
TEST ! dd if=/dev/zero of=$M0/c bs=5M count=1

rm -rf $M0/*
#Size will reserve from the previously set reserve option = 20%
TEST $CLI volume set $V0 storage.reserve-size 0

#Overwrite reserve option
TEST $CLI volume set $V0 storage.reserve-size 40MB

#wait 5s to reset disk_space_full flag
sleep 5

TEST dd if=/dev/zero of=$M0/a bs=100M count=1
TEST dd if=/dev/zero of=$M0/b bs=10M count=1

# Wait 5s to update disk_space_full flag because thread check disk space
# after every 5s

sleep 5
# setup_lvm create lvm partition of 150M and 40M are reserve so after
# consuming more than 110M next dd should fail
TEST ! dd if=/dev/zero of=$M0/c bs=5M count=1

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
