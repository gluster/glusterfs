#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function get_uid() {
    stat -c '%u' $1;
}

function get_gid() {
    stat -c '%g' $1;
}

function check_stat() {
    stat $1
    echo $?
}

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '6' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

EXPECT 0 get_uid $M0;
EXPECT 0 get_gid $M0;

TEST chown 100:101 $M0;

EXPECT 100 get_uid $M0;
EXPECT 101 get_gid $M0;

TEST $CLI volume stop $V0;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" online_brick_count

TEST $CLI volume start $V0;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "6" online_brick_count

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 4
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 5

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" check_stat $M0
EXPECT 100 get_uid $M0;
EXPECT 101 get_gid $M0;

cleanup;
