#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B{0..1}/$V0
TEST $CLI volume set $V0 readdir-ahead on

DIRECTORY="$M0/subdir1/subdir2"

#Make sure md-cache has large timeout to hold stat from readdirp_cbk in its cache
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume start $V0
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
rm -rf $M0/*
TEST mkdir -p $DIRECTORY
rm -rf $DIRECTORY/*
TEST touch $DIRECTORY/file{0..10}
rdd_tester=$(dirname $0)/rdd-tester
TEST build_tester $(dirname $0)/bug-1390050.c -o $rdd_tester
TEST $rdd_tester $DIRECTORY $DIRECTORY/file4
rm -f $rdd_tester
cleanup;

