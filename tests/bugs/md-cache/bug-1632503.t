#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TESTS_EXPECTED_IN_LOOP=5

TEST glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

TEST $CLI volume start $V0

TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.md-cache-statfs on

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

for i in $(seq 1 5); do
    TEST_IN_LOOP df $M0;
done

cleanup;
