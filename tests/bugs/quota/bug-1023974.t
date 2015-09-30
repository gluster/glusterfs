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
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4,5,6};
TEST $CLI volume start $V0;

TEST $CLI volume quota $V0 enable;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST mkdir -p $M0/1/2;
TEST $CLI volume quota $V0 limit-usage /1/2 100MB 70%;
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0

#The corresponding write(3) should fail with EDQUOT ("Disk quota exceeded")
TEST ! $QDD $M0/1/2/file 256 408
TEST mkdir -p $M0/1/3;
TEST $QDD $M0/1/3/file 256 408

#The corresponding rename(3) should fail with EDQUOT ("Disk quota exceeded")
TEST ! mv $M0/1/3/ $M0/1/2/3_mvd;

TEST $CLI volume stop $V0
EXPECT "1" get_aux

rm -f $QDD

cleanup;
