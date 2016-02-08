#!/bin/bash

TESTS_EXPECTED_IN_LOOP=10

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function get_socket_count() {
         netstat -nap | grep $1 | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0  $H0:$B0/$V0
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST ls $M0

GLFS_MNT_PID=`ps ax | grep "glusterfs -s $H0 \-\-volfile\-id $V0 $M0" | sed -e "s/^ *\([0-9]*\).*/\1/g"`

SOCKETS_BEFORE_SWITCH=`netstat -nap | grep $GLFS_MNT_PID | grep ESTABLISHED | wc -l`

for i in $(seq 1 5); do
    TEST_IN_LOOP $CLI volume set $V0 performance.write-behind off;
    sleep 1;
    TEST_IN_LOOP $CLI volume set $V0 performance.write-behind on;
    sleep 1;
done

SOCKETS_AFTER_SWITCH=`netstat -nap | grep $GLFS_MNT_PID | grep ESTABLISHED | wc -l`

# currently active graph is not cleaned up till some operation on
# mount-point. Hence there is one extra graph.
TEST [ $SOCKETS_AFTER_SWITCH = `expr $SOCKETS_BEFORE_SWITCH + 1` ]

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
