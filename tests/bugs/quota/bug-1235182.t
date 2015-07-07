#!/bin/bash

# This regression test tries to ensure renaming a directory with content, and
# no limit set, is accounted properly, when moved into a directory with quota
# limit set.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TEST glusterd
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0};
TEST $CLI volume start $V0;

TEST $CLI volume quota $V0 enable;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST $CLI volume quota $V0 limit-usage / 1GB
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0

$QDD $M0/f1 256 400&
PID=$!
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" STAT $M0/f1
TESTS_EXPECTED_IN_LOOP=50
for i in {1..50}; do
        ii=`expr $i + 1`;
        TEST_IN_LOOP mv $M0/f$i $M0/f$ii;
done

echo "Wait for process with pid $PID to complete"
wait $PID
echo "Process with pid $PID finished"

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "100.0MB" quotausage "/"

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
EXPECT "1" get_aux

rm -f $QDD

cleanup;
