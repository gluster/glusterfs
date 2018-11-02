#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

function atime_compare {
    local atime=$1
    local file_name=$2
    local atime1=$(stat -c "%X" $file_name)

    if [ $atime == $atime1 ]
    then
        echo "0"
    else
        echo "1"
    fi
}

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

cd $M0
TEST "echo hello_world > FILE"
atime1=$(stat -c "%X" FILE)

TEST "cat FILE > /dev/null"
EXPECT "0" atime_compare $atime1 FILE

sleep 1

TEST $CLI volume set $V0 noatime off
TEST "cat FILE > /dev/null"
EXPECT "1" atime_compare $atime1 FILE

cd -
cleanup
