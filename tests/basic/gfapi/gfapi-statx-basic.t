#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 ${H0}:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

# NOTE: Test is passing due to very specific volume configuration
# Disable md-cache, as it does not save and return ia_flags from iatt
# This is possibly going to be true of other xlators as well (ec/afr), need to
# ensure these are fixed, or hack statx to return all basic attrs anyway.
TEST $CLI volume set $V0 performance.md-cache-timeout 0

logdir=`gluster --print-logdir`

build_tester $(dirname $0)/gfapi-statx-basic.c -lgfapi

TEST ./$(dirname $0)/gfapi-statx-basic $V0 $logdir/gfapi-statx-basic.log

cleanup_tester $(dirname $0)/gfapi-statx-basic

cleanup;
