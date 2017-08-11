#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

EXPECT_NOT "-1" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep read_child |awk '{print $3}'`
TEST $CLI volume set $V0 choose-local off
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "-1" echo `cat $M0/.meta/graphs/active/$V0-replicate-0/private|grep read_child |awk '{print $3}'`

cleanup
