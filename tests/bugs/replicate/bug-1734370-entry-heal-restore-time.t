#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

function time_stamps_match {
        path=$1
        mtime_source_b0=$(get_mtime $B0/${V0}0/$path)
        atime_source_b0=$(get_atime $B0/${V0}0/$path)
        mtime_source_b2=$(get_mtime $B0/${V0}2/$path)
        atime_source_b2=$(get_atime $B0/${V0}2/$path)
        mtime_sink_b1=$(get_mtime $B0/${V0}1/$path)
        atime_sink_b1=$(get_atime $B0/${V0}1/$path)

        #The same brick must be the source of heal for both atime and mtime.
        if [[ ( $mtime_source_b0 -eq $mtime_sink_b1 && $atime_source_b0 -eq $atime_sink_b1 ) || \
              ( $mtime_source_b2 -eq $mtime_sink_b1 && $atime_source_b2 -eq $atime_sink_b1 ) ]]
        then
            echo "Y"
        else
            echo "Mtimes: $mtime_source_b0:$mtime_sink_b1:$mtime_source_b2 Atimes: $atime_source_b0:$atime_sink_b1:$atime_source_b2"
        fi

}

function mtimes_match {
        path=$1
        mtime_source_b0=$(get_mtime $B0/${V0}0/$path)
        mtime_source_b2=$(get_mtime $B0/${V0}2/$path)
        mtime_sink_b1=$(get_mtime $B0/${V0}1/$path)

        if [[ ( $mtime_source_b0 -eq $mtime_sink_b1) || \
              ( $mtime_source_b2 -eq $mtime_sink_b1) ]]
        then
            echo "Y"
        else
            echo "Mtimes: $mtime_source_b0:$mtime_sink_b1:$mtime_source_b2"
        fi

}

# Test that the parent dir's timestamps are restored during entry-heal.
GET_MDATA_PATH=$(dirname $0)/../../utils
build_tester $GET_MDATA_PATH/get-mdata-xattr.c

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume start $V0;

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

###############################################################################
TEST mkdir $M0/DIR
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST touch $M0/DIR/FILE

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

EXPECT "Y" time_stamps_match DIR
ctime_source1=$(get_ctime $B0/${V0}0/$path)
ctime_source2=$(get_ctime $B0/${V0}2/$path)
ctime_sink=$(get_ctime $B0/${V0}1/$path)
TEST [ $ctime_source1 -eq $ctime_sink ]
TEST [ $ctime_source2 -eq $ctime_sink ]


###############################################################################
# Repeat the test with ctime feature disabled.
TEST $CLI volume set $V0 features.ctime off
TEST mkdir $M0/DIR2
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST touch $M0/DIR2/FILE

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
#Executing parallel heal may lead to changing atime after heal. So better
#to test just the mtime
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

EXPECT "Y" mtimes_match DIR2

TEST rm $GET_MDATA_PATH/get-mdata-xattr
cleanup;
