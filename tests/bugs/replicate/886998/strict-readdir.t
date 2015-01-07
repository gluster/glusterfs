#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc

function num_files_in_dir {
        d=$1
        ls $d | sort | uniq | wc -l
}

#Basic sanity tests for readdir functionality
cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/r2d2_0 $H0:$B0/r2d2_1 $H0:$B0/r2d2_2 $H0:$B0/r2d2_3
TEST $CLI volume start $V0
TEST glusterfs --volfile-server=$H0 --volfile-id=/$V0 $M0

TEST touch $M0/{1..100}
EXPECT "100" num_files_in_dir $M0

TEST kill_brick $V0 $H0 $B0/r2d2_0
TEST kill_brick $V0 $H0 $B0/r2d2_2
EXPECT "100" num_files_in_dir $M0

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST kill_brick $V0 $H0 $B0/r2d2_1
TEST kill_brick $V0 $H0 $B0/r2d2_3
EXPECT "100" num_files_in_dir $M0

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 3

TEST $CLI volume set $V0 cluster.strict-readdir on
EXPECT "on" volinfo_field $V0 cluster.strict-readdir
TEST kill_brick $V0 $H0 $B0/r2d2_0
TEST kill_brick $V0 $H0 $B0/r2d2_2
EXPECT "100" num_files_in_dir $M0

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST kill_brick $V0 $H0 $B0/r2d2_1
TEST kill_brick $V0 $H0 $B0/r2d2_3
EXPECT "100" num_files_in_dir $M0
cleanup;
