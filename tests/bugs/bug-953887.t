#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fileio.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/10
TEST fd=`fd_available`
TEST fd_open $fd 'w' $M0/10
TEST gluster volume add-brick $V0 $H0:$B0/${V0}{2,3}
TEST gluster volume rebalance $V0 start
EXPECT_WITHIN 15 "completed" rebalance_status_field $V0
TEST cat $M0/10
TEST fd_write $fd "abc"
EXPECT "abc" echo "$(cat $M0/10)"
cleanup
