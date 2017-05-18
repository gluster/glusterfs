#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/glfs_xreaddirplus_r.c -lgfapi

TEST $(dirname $0)/glfs_xreaddirplus_r $H0 $V0  $logdir/glfs_xreaddirplus_r.log

cleanup_tester $(dirname $0)/glfs_xreaddirplus_r

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
