#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=40

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST mkdir $M0/dir

for i in {1..20}; do
        TEST_IN_LOOP touch $M0/dir/$i;
done

TEST $CLI volume set $V0 features.shard on

TEST ls $M0
TEST ls $M0/dir

for i in {1..10}; do
        TEST_IN_LOOP mv $M0/dir/$i $M0/dir/$i-sharded;
done

for i in {11..20}; do
        TEST_IN_LOOP unlink $M0/dir/$i;
done

cleanup;
