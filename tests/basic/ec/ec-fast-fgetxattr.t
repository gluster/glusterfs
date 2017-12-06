#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{1..6}
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.client-io-threads off
TEST $CLI volume set $V0 brick-log-level DEBUG
TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.delay-duration 10000000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable read,write

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
TEST touch $M0/file

# Perform two writes to make sure io-threads have enough threads to perform
# things in parallel when the test execution happens.
echo abc > $M0/file1 &
echo abc > $M0/file2 &
wait

TEST build_tester $(dirname $0)/ec-fast-fgetxattr.c -lgfapi -Wall -O2
TEST $(dirname $0)/ec-fast-fgetxattr $H0 $V0 /file
cleanup_tester $(dirname ${0})/ec-fast-fgetxattr

cleanup;
