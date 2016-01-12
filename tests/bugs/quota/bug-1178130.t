#!/bin/bash

# This regression test tries to ensure renaming a directory with content, and
# no limit set, is accounted properly, when moved into a directory with quota
# limit set.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TEST glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0;

TEST $CLI volume quota $V0 enable;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST $CLI volume quota $V0 limit-usage / 500MB
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0

TEST $QDD $M0/file 256 40
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quotausage "/"

TEST kill_brick $V0 $H0 $B0/${V0}2
TEST mv $M0/file $M0/file2
TEST $CLI volume start $V0 force;

#wait for self heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "0" STAT "$B0/${V0}2/file2"

#usage should remain same after rename and self-heal operation
EXPECT "10.0MB" quotausage "/"

rm -f $QDD

cleanup;
