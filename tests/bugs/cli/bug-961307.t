#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

REPLICA=2

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica $REPLICA $H0:$B0/${V0}-00 $H0:$B0/${V0}-01 $H0:$B0/${V0}-10 $H0:$B0/${V0}-11
TEST $CLI volume start $V0

var1=$(gluster volume remove-brick $H0:$B0/${V0}-00 $H0:$B0/${V0}-01 start 2>&1)
var2="volume remove-brick start: failed: Volume $H0:$B0/${V0}-00 does not exist"

EXPECT "$var2" echo "$var1"
cleanup;
