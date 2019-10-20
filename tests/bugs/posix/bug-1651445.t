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

#Setting the size in bytes
TEST $CLI volume set $V0 storage.reserve 40MB

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
TEST dd if=/dev/urandom of=$M0/a  bs=1022 count=1  oflag=seek_bytes,sync seek=102 conv=notrunc

rm -rf $M0/*

#Setting the size in percent and repeating the above steps
TEST $CLI volume set $V0 storage.reserve 40

sleep 5

TEST dd if=/dev/zero of=$M0/a bs=80M count=1
TEST dd if=/dev/zero of=$M0/b bs=10M count=1

sleep 5
TEST ! dd if=/dev/zero of=$M0/c bs=5M count=1

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
