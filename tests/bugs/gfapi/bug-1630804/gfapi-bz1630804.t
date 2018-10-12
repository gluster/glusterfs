#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 ${H0}:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

build_tester $(dirname $0)/gfapi-bz1630804.c -lgfapi

TEST ./$(dirname $0)/gfapi-bz1630804 $V0 $logdir/gfapi-bz1630804.log 0
TEST ./$(dirname $0)/gfapi-bz1630804 $V0 $logdir/gfapi-bz1630804.log 1

cleanup_tester $(dirname $0)/gfapi-trunc

cleanup;
