#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST truncate -s 100M $B0/brick1

TEST L1=`SETUP_LOOP $B0/brick1`
TEST MKFS_LOOP $L1

TEST mkdir -p $B0/${V0}1

TEST MOUNT_LOOP $L1 $B0/${V0}1

TEST $CLI volume create $V0 $H0:$B0/${V0}1

TEST $CLI volume start $V0;

TEST glusterfs -s $H0 --volfile-id=$V0 $M0
TEST touch $M0/foo
TEST build_tester $(dirname $0)/zero-fill-enospace.c -lgfapi -Wall -O2
TEST ! $(dirname $0)/zero-fill-enospace $H0 $V0 /foo `gluster --print-logdir`/glfs-$V0.log 104857600

TEST force_umount $M0
TEST $CLI volume stop $V0
UMOUNT_LOOP ${B0}/${V0}1
rm -f ${B0}/brick1

cleanup
