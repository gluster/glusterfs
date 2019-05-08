#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fallocate.rc

cleanup

require_fallocate -l 1m $M0/file

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST fallocate -l 200M $M0/foo
EXPECT `echo "$(( ( 200 * 1024 * 1024 ) / 512 ))"`  stat -c %b $M0/foo
TEST truncate -s 0 $M0/foo
EXPECT "0" stat -c %b $M0/foo
TEST fallocate -l 100M $M0/foo
EXPECT `echo "$(( ( 100 * 1024 * 1024 ) / 512 ))"`  stat -c %b $M0/foo

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
