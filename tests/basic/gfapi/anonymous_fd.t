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

TEST build_tester $(dirname $0)/anonymous_fd_read_write.c -lgfapi -o $(dirname $0)/anonymous_fd
TEST ./$(dirname $0)/anonymous_fd $H0 $V0  $logdir/anonymous_fd.log

cleanup_tester $(dirname $0)/anonymous_fd

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
