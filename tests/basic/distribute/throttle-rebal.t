#!/bin/bash
# Test to check
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#Check rebal-throttle set option sanity
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

function set_throttle {
        local level=$1
        $CLI volume set $V0 cluster.rebal-throttle $level 2>&1 |grep -oE 'success|failed'
}


THROTTLE_LEVEL="lazy"
EXPECT "success" set_throttle $THROTTLE_LEVEL
EXPECT "$THROTTLE_LEVEL" echo `$CLI volume info | grep rebal-throttle | awk '{print $2}'`

THROTTLE_LEVEL="normal"
EXPECT "success" set_throttle $THROTTLE_LEVEL
EXPECT "$THROTTLE_LEVEL" echo `$CLI volume info | grep rebal-throttle | awk '{print $2}'`


THROTTLE_LEVEL="aggressive"
EXPECT "success" set_throttle $THROTTLE_LEVEL
EXPECT "$THROTTLE_LEVEL" echo `$CLI volume info | grep rebal-throttle | awk '{print $2}'`

THROTTLE_LEVEL="garbage"
EXPECT "failed" set_throttle $THROTTLE_LEVEL

#check if throttle-level is still aggressive
EXPECT "aggressive" echo `$CLI volume info | grep rebal-throttle | awk '{print $2}'`

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
