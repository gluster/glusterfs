#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{1..6}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 disperse.eager-lock-timeout 5

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
TEST touch $M0/file

TEST build_tester $(dirname $0)/ec-badfd.c -lgfapi -Wall -O2
TEST $(dirname $0)/ec-badfd $H0 $V0 /file
cleanup_tester $(dirname ${0})/ec-badfd

cleanup;
