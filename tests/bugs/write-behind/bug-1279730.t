#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

## Start and create a volume
TEST glusterd
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0;
TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 4
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0

# compile the test program and run it
TEST $CC -O0 -g3 $(dirname $0)/bug-1279730.c -o $(dirname $0)/bug-1279730

TEST $(dirname $0)/bug-1279730 $M0/file "\"$CLI volume quota $V0 limit-usage / 1024\""

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;

#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1279730
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1279730

