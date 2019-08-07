#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 network.remote-dio off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.strict-o-direct on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST dd if=/dev/zero of=$M0/metadata bs=501 count=1

EXPECT "501" echo $("dd" if=$M0/metadata bs=4096 count=1 of=/dev/null iflag=direct 2>&1 | awk '/bytes/ {print $1}')

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
