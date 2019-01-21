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

volname="StartMigrationDuringRebalanceTest"
TEST glusterd
TEST pidof glusterd;

TEST $CLI volume info;
TEST $CLI volume create $volname $H0:$B0/${volname}{1..4};
TEST $CLI volume start $volname;

#bug-1046308 - validate rebalance on a specified volume name
TEST $CLI volume rebalance $volname start;

#bug-1089668 - validation of rebalance status and remove brick status
#bug-963541 - after remove brick start rebalance/remove brick start without commiting should fail

TEST ! $CLI volume remove-brick $volname $H0:$B0/${volname}1 status

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $volname

TEST $CLI volume remove-brick $volname $H0:$B0/${volname}1 start
TEST ! $CLI volume rebalance $volname start
TEST ! $CLI volume rebalance $volname status
TEST ! $CLI volume remove-brick $volname $H0:$B0/${volname}2 start

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field \
"$volname" "$H0:$B0/${volname}1"
TEST $CLI volume remove-brick $volname $H0:$B0/${volname}1 commit

TEST $CLI volume rebalance $volname start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $volname
TEST $CLI volume rebalance $volname stop

TEST $CLI volume remove-brick $volname $H0:$B0/${volname}2 start
TEST $CLI volume remove-brick $volname $H0:$B0/${volname}2 stop

#bug-1351021-rebalance-info-post-glusterd-restart

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

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#bug-1004744 - validation of rebalance fix layout

TEST $CLI volume start $V0 force
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

for i in `seq 11 20`;
do
       mkdir $M0/dir_$i
       echo file>$M0/dir_$i/file_$i
       for j in `seq 1 100`;
       do
                mkdir $M0/dir_$i/dir_$j
                echo file>$M0/dir_$i/dir_$j/file_$j
       done
done

#add 2 bricks
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{5,6};

#perform rebalance fix-layout
TEST $CLI volume rebalance $V0 fix-layout start

EXPECT_WITHIN $REBALANCE_TIMEOUT "fix-layout completed" fix-layout_status_field $V0;

#bug-1075087 - rebalance post add brick
TEST mkdir $M0/dir{21..30};
TEST touch $M0/dir{21..30}/files{1..10};

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}{7,8}

TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN 180 "completed" rebalance_status_field $V0

TEST pkill gluster
TEST glusterd
TEST pidof glusterd

# status should be "completed" immediate after glusterd has respawned.
EXPECT_WITHIN 20 "completed" rebalance_status_field $V0

cleanup
