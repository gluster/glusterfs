#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc


# This test opens 100 fds and triggers graph switches to check if fsync
# as part of graph-switch causes crash or not.

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST touch $M0/{1..100}
for i in {1..100}; do fd[$i]=`fd_available`; fd_open ${fd[$i]} 'w' $M0/$i; done
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{2,3}
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
TEST cat $M0/{1..100}
for i in {1..100}; do fd_write ${fd[$i]} 'abc'; done
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{4,5}
TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0
for i in {1..100}; do fd_write ${fd[$i]} 'abc'; done
TEST cat $M0/{1..100}
cleanup
