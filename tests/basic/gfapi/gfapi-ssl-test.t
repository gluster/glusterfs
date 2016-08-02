#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../ssl.rc

cleanup;

TEST create_self_signed_certs

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/gfapi-ssl-test.c -lgfapi

# Run test without I/O or management encryption
TEST ./$(dirname $0)/gfapi-ssl-test $H0 $V0 $logdir/gfapi-ssl-test.log

# Enable management encryption
touch  $GLUSTERD_WORKDIR/secure-access

killall_gluster

TEST glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count

# Run test with management encryption (No I/O encryption)
TEST ./$(dirname $0)/gfapi-ssl-test $H0 $V0 $logdir/gfapi-ssl-test.log

# Enable I/O encryption
TEST $CLI volume set $V0 client.ssl on
TEST $CLI volume set $V0 server.ssl on

killall_gluster

TEST glusterd
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count

# Run test without I/O or management encryption
TEST ./$(dirname $0)/gfapi-ssl-test $H0 $V0 $logdir/gfapi-ssl-test.log

cleanup_tester $(dirname $0)/gfapi-ssl-test

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;

# NetBSD build scripts are not upto date therefore this test
# is failing in NetBSD. Therefore skipping the test in NetBSD
# as of now.
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
