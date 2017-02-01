#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0

TEST $CLI volume set $V0 group metadata-cache
EXPECT 'on' volinfo_field $V0 'performance.cache-invalidation'
EXPECT '600' volinfo_field $V0 'performance.md-cache-timeout'
EXPECT 'on' volinfo_field $V0 'performance.stat-prefetch'
EXPECT '600' volinfo_field $V0 'features.cache-invalidation-timeout'
EXPECT 'on' volinfo_field $V0 'features.cache-invalidation'
EXPECT '50000' volinfo_field $V0  'network.inode-lru-limit'
cleanup;
