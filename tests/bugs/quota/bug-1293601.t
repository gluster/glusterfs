#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TESTS_EXPECTED_IN_LOOP=1044

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4}
TEST $CLI volume start $V0
TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 2MB

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..1024}; do
    TEST_IN_LOOP dd if=/dev/zero of=$M0/f$i bs=1k count=1
done

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "1.0MB" quotausage "/"

for i in {1..10}; do
    TEST_IN_LOOP $CLI volume quota $V0 disable
    TEST_IN_LOOP $CLI volume quota $V0 enable
done

TEST $CLI volume quota $V0 limit-usage / 2MB
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "1.0MB" quotausage "/"

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
cleanup;
