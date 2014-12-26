#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3
TEST $CLI volume set $V0 cluster.eager-lock on
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir $M0/dir{1..10};
TEST touch $M0/dir{1..10}/files{1..10};

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}4 $H0:/$B0/${V0}5

TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

cleanup;
