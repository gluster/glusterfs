#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

TESTS_EXPECTED_IN_LOOP=44
SCRIPT_TIMEOUT=600

rename_files() {
    MOUNT=$1
    ITERATIONS=$2
    for i in $(seq 1 $ITERATIONS); do uuid="`uuidgen`"; echo "some data" > $MOUNT/test$uuid; mv $MOUNT/test$uuid $MOUNT/test -f || return $?; done
}

run_test_for_volume() {
    VOLUME=$1
    ITERATIONS=$2
    TEST_IN_LOOP $CLI volume start $VOLUME

    TEST_IN_LOOP glusterfs -s $H0 --volfile-id $VOLUME $M0
    TEST_IN_LOOP glusterfs -s $H0 --volfile-id $VOLUME $M1
    TEST_IN_LOOP glusterfs -s $H0 --volfile-id $VOLUME $M2
    TEST_IN_LOOP glusterfs -s $H0 --volfile-id $VOLUME $M3

    rename_files $M0 $ITERATIONS &
    M0_RENAME_PID=$!

    rename_files $M1 $ITERATIONS &
    M1_RENAME_PID=$!

    rename_files $M2 $ITERATIONS &
    M2_RENAME_PID=$!

    rename_files $M3 $ITERATIONS &
    M3_RENAME_PID=$!

    TEST_IN_LOOP wait $M0_RENAME_PID
    TEST_IN_LOOP wait $M1_RENAME_PID
    TEST_IN_LOOP wait $M2_RENAME_PID
    TEST_IN_LOOP wait $M3_RENAME_PID

    TEST_IN_LOOP $CLI volume stop $VOLUME
    TEST_IN_LOOP $CLI volume delete $VOLUME
    umount $M0 $M1 $M2 $M3
}

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{0..8} force
run_test_for_volume $V0 200

TEST $CLI volume create $V0 replica 3 arbiter 1 $H0:$B0/${V0}{0..8} force
run_test_for_volume $V0 200

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..8} force
run_test_for_volume $V0 200

TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5} force
run_test_for_volume $V0 200

cleanup
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
