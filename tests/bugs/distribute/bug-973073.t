#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc

## Steps followed are one described in bugzilla

cleanup;

function get_layout()
{
        layout1=`getfattr -n trusted.glusterfs.dht -e hex $1 2>&1`

        if [ $? -ne 0 ]
        then
                echo 1
        else
                echo 0
        fi

}

BRICK_COUNT=3

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 start

## remove-brick status == rebalance_status
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" remove_brick_completed

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 stop

TEST $CLI volume rebalance $V0 fix-layout start

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed

TEST mkdir $M0/dir 2>/dev/null;

EXPECT "0" get_layout $B0/${V0}2/dir
cleanup;
