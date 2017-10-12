#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../dht.rc


# Check if every single rebalance process migrated some files

function cluster_rebal_all_nodes_migrated_files {
        val=0
        a=$($CLI_1 volume rebalance $V0 status | grep "completed" | awk '{print $2}');
#        echo $a
        b=($a)
        for i in "${b[@]}"
        do
#                echo "$i";
                if [ "$i" -eq "0" ]; then
                        echo "false";
                        val=1;
                fi
        done
        echo $val
}

cleanup

TEST launch_cluster 3;
TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count


#Start with a pure distribute volume (multiple bricks on the same node)
TEST $CLI_1 volume create $V0 $H1:$B1/dist1 $H1:$B1/dist2 $H2:$B2/dist3 $H2:$B2/dist4

TEST $CLI_1 volume start $V0
$CLI_1 volume info $V0

#TEST $CLI_1 volume set $V0 client-log-level DEBUG

## Mount FUSE
TEST glusterfs -s $H1 --volfile-id $V0 $M0;

TEST mkdir $M0/dir1 2>/dev/null;
TEST touch $M0/dir1/file-{1..500}

## Add-brick and run rebalance to force file migration
TEST $CLI_1 volume add-brick $V0 $H1:$B1/dist5 $H2:$B2/dist6

#Start a rebalance
TEST $CLI_1 volume rebalance $V0 start force

#volume rebalance status should work
#TEST $CLI_1 volume rebalance $V0 status
#$CLI_1 volume rebalance $V0 status

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" cluster_rebalance_completed
EXPECT "0" cluster_rebal_all_nodes_migrated_files
$CLI_1 volume rebalance $V0 status


TEST umount -f $M0
TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume delete $V0


##############################################################

# Next, a dist-rep volume
TEST $CLI_1 volume create $V0 replica 2 $H1:$B1/drep1 $H2:$B2/drep1 $H1:$B1/drep2 $H2:$B2/drep2

TEST $CLI_1 volume start $V0
$CLI_1 volume info $V0

#TEST $CLI_1 volume set $V0 client-log-level DEBUG

## Mount FUSE
TEST glusterfs -s $H1 --volfile-id $V0 $M0;

TEST mkdir $M0/dir1 2>/dev/null;
TEST touch $M0/dir1/file-{1..500}

## Add-brick and run rebalance to force file migration
TEST $CLI_1 volume add-brick $V0 replica 2 $H1:$B1/drep3 $H2:$B2/drep3

#Start a rebalance
TEST $CLI_1 volume rebalance $V0 start force

#volume rebalance status should work
#TEST $CLI_1 volume rebalance $V0 status
#$CLI_1 volume rebalance $V0 status

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" cluster_rebalance_completed
#EXPECT "0" cluster_rebal_all_nodes_migrated_files
$CLI_1 volume rebalance $V0 status


TEST umount -f $M0
TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume delete $V0

##############################################################

# Next, a disperse volume
TEST $CLI_1 volume create $V0 disperse 3 $H1:$B1/ec1 $H2:$B1/ec2 $H3:$B1/ec3 force

TEST $CLI_1 volume start $V0
$CLI_1 volume info $V0

#TEST $CLI_1 volume set $V0 client-log-level DEBUG

## Mount FUSE
TEST glusterfs -s $H1 --volfile-id $V0 $M0;

TEST mkdir $M0/dir1 2>/dev/null;
TEST touch $M0/dir1/file-{1..500}

## Add-brick and run rebalance to force file migration
TEST $CLI_1 volume add-brick $V0 $H1:$B2/ec4 $H2:$B2/ec5 $H3:$B2/ec6

#Start a rebalance
TEST $CLI_1 volume rebalance $V0 start force

#volume rebalance status should work
#TEST $CLI_1 volume rebalance $V0 status
#$CLI_1 volume rebalance $V0 status

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" cluster_rebalance_completed

# this will not work unless EC is changed to return all node-uuids
# comment this out once that patch is ready
#EXPECT "0" cluster_rebal_all_nodes_migrated_files
$CLI_1 volume rebalance $V0 status


TEST umount -f $M0
TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume delete $V0

##############################################################

cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=1501388
