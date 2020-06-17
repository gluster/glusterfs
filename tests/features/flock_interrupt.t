#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}0;

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST touch $M0/testfile;

echo > got_lock
flock $M0/testfile sleep 6 & { sleep 0.3; flock -w 2 $M0/testfile true; echo ok > got_lock; } &

EXPECT_WITHIN 4 ok cat got_lock;

## Finish up
rm -f got_lock;
cleanup;
