#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

trap "jobs -p | xargs -r kill -9" EXIT INT QUIT

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 diagnostics.client-log-level TRACE
EXPECT 'Created' volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'
TEST glusterfs --attribute-timeout=0 --entry-timeout=0 --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

TEST mkdir -p $M0/a/b/c
dd if=/dev/zero of=$M0/a/b/c/test bs=1024k &

sleep 1

TEST gluster volume replace-brick $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}3 commit force

sleep 10

TEST kill -TERM %1
wait %1
# Verify that the 'dd' process was terminated by the 'kill -TERM' and not by
# any other error.
TEST [ $? -eq 143 ]

cleanup
