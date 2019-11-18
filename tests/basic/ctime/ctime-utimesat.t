#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.read-after-open off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.io-cache off

TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

touch $M0/FILE

atime=$(stat -c "%.X" $M0/FILE)
EXPECT $atime stat -c "%.Y" $M0/FILE
EXPECT $atime stat -c "%.Z" $M0/FILE

cleanup
