#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;


TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 3


EXPECT 1 echo `$CLI volume heal $V0 statistics heal-count replica $H0:$B0/${V0}0 | grep -A 1 ${V0}0 | grep "entries" | wc -l`
EXPECT 1 echo `$CLI volume heal $V0 statistics heal-count replica $H0:$B0/${V0}1 | grep -A 1 ${V0}1 | grep "entries" | wc -l`
EXPECT 1 echo `$CLI volume heal $V0 statistics heal-count replica $H0:$B0/${V0}2 | grep -A 1 ${V0}2 | grep "entries" | wc -l`
EXPECT 1 echo `$CLI volume heal $V0 statistics heal-count replica $H0:$B0/${V0}3 | grep -A 1 ${V0}3 | grep "entries" | wc -l`

cleanup
