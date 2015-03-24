#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Upcall feature is disable for now. A new xlator option
# will be introduced to turn it on. Skipping this test
# till then.

SKIP_TESTS;
exit 0

TEST glusterd

TEST $CLI volume create $V0 localhost:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

## Enable Upcall cache-invalidation feature
TEST $CLI volume set $V0 features.cache-invalidation on;

build_tester $(dirname $0)/upcall-cache-invalidate.c -lgfapi -o $(dirname $0)/upcall-cache-invalidate

TEST ./$(dirname $0)/upcall-cache-invalidate $V0  $logdir/upcall-cache-invalidate.log

cleanup_tester $(dirname $0)/upcall-cache-invalidate

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
