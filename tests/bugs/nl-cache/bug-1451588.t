#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{0..4}
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume set $V0 performance.nl-cache on
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --volfile-id=/$V0 --aux-gfid-mount --volfile-server=$H0 $M0

TEST ! stat $M0/.gfid/1901b1a0-c612-46ee-b45a-e8345d5a0b48

cleanup;
G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
