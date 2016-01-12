#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TEST glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2;
TEST $CLI volume start $V0;

TEST $CLI volume quota $V0 enable;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST $CLI volume quota $V0 limit-usage / 11MB
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0

TEST $QDD $M0/f1 256 40

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quotausage "/"

if [ -f "$B0/${V0}1/f1" ]; then
        HASHED="$B0/${V0}1"
        OTHER="$B0/${V0}2"
else
        HASHED="$B0/${V0}2"
        OTHER="$B0/${V0}1"
fi

TEST $CLI volume remove-brick $V0 $H0:${HASHED} start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:${HASHED}";

#check consistency in mount point and also check that file is migrated to OTHER
TEST [ -f "$OTHER/f1" ];
TEST [ -f "$M0/f1" ];

#check that remove-brick status should not have any failed or skipped files
var=`$CLI volume remove-brick $V0 $H0:${HASHED} status | grep completed`
TEST [ `echo $var | awk '{print $5}'` = "0"  ]
TEST [ `echo $var | awk '{print $6}'` = "0"  ]

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quotausage "/"

rm -f $QDD
cleanup;
