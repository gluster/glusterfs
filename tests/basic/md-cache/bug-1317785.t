#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0

TEST $CLI volume set $V0 cache-swift-metadata on
EXPECT 'on' volinfo_field $V0 'performance.cache-swift-metadata'

TEST $CLI volume set $V0 cache-swift-metadata off
EXPECT 'off' volinfo_field $V0 'performance.cache-swift-metadata'

TEST $CLI volume set $V0 performance.cache-capability-xattrs off
EXPECT 'off' volinfo_field $V0 'performance.cache-capability-xattrs'

TEST $CLI volume set $V0 performance.cache-capability-xattrs on
EXPECT 'on' volinfo_field $V0 'performance.cache-capability-xattrs'

TEST $CLI volume set $V0 performance.cache-ima-xattrs off
EXPECT 'off' volinfo_field $V0 'performance.cache-ima-xattrs'

TEST $CLI volume set $V0 performance.cache-ima-xattrs on
EXPECT 'on' volinfo_field $V0 'performance.cache-ima-xattrs'

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
