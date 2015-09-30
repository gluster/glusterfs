#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0;

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0

# compile the test program and run it
TEST $CC $(dirname $0)/bug-1058663.c -o $(dirname $0)/bug-1058663;
TEST $(dirname $0)/bug-1058663 $M0/bug-1058663.bin;
TEST rm -f $(dirname $0)/M0/bug-1058663.bin;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
