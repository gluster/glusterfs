#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
EXIT_EARLY=1;

TEST glusterd

TEST $CLI volume create $V0 ${H0}:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/gfapi-async-calls-test.c -lgfapi


TEST ./$(dirname $0)/gfapi-async-calls-test ${H0} $V0 $logdir/gfapi-async-calls-test.log

cleanup_tester $(dirname $0)/gfapi-async-calls-test

cleanup;
