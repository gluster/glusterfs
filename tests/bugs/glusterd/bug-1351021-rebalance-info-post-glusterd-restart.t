#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc

cleanup;

function get_rebalanced_info()
{
        local rebal_info_key=$2
        $CLI  volume rebalance $1 status | awk '{print $'$rebal_info_key'}' |sed -n 3p| sed 's/ *$//g'
}


TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3};
TEST $CLI volume start $V0;

#Mount volume and create data
TEST glusterfs -s $H0 --volfile-id $V0 $M0;
TEST mkdir $M0/dir{1..10}
TEST touch $M0/dir{1..10}/file{1..10}

# Add-brick and start rebalance
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}4
TEST $CLI volume rebalance $V0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

#Rebalance info before glusterd restart
OLD_REBAL_FILES=$(get_rebalanced_info $V0 2)
OLD_SIZE=$(get_rebalanced_info $V0 3)
OLD_SCANNED=$(get_rebalanced_info $V0 4)
OLD_FAILURES=$(get_rebalanced_info $V0 5)
OLD_SKIPPED=$(get_rebalanced_info $V0 6)


pkill glusterd;
pkill glusterfsd;
TEST glusterd

#Rebalance info after glusterd restart
NEW_REBAL_FILES=$(get_rebalanced_info $V0 2)
NEW_SIZE=$(get_rebalanced_info $V0 3)
NEW_SCANNED=$(get_rebalanced_info $V0 4)
NEW_FAILURES=$(get_rebalanced_info $V0 5)
NEW_SKIPPED=$(get_rebalanced_info $V0 6)

#Check rebalance info before and after glusterd restart
TEST [ $OLD_REBAL_FILES == $NEW_REBAL_FILES ]
TEST [ $OLD_SIZE == $NEW_SIZE ]
TEST [ $OLD_SCANNED == $NEW_SCANNED ]
TEST [ $OLD_FAILURES == $NEW_FAILURES ]
TEST [ $OLD_SKIPPED == $NEW_SKIPPED ]

cleanup;

