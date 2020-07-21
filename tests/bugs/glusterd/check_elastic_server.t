#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc

function cluster_rebalance_status {
        local vol=$1
        $CLI_2 volume status | grep -iw "Rebalance" -A 5 | grep "Status" | sed 's/.*: //'
}

cleanup;
TEST launch_cluster 4;
TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
TEST $CLI_1 peer probe $H4;

EXPECT_WITHIN $PROBE_TIMEOUT 3 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0  $H2:$B2/$V0
EXPECT 'Created' cluster_volinfo_field 1 $V0 'Status';

$CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

#Mount invalid volume
TEST ! glusterfs -s $H1 --volfile-id=$V0_NA $M0;

#Mount FUSE
TEST glusterfs -s $H1 --volfile-id=$V0 $M0;

TEST mkdir $M0/dir{1..4};
TEST touch $M0/dir{1..4}/files{1..4};

TEST $CLI_1 volume remove-brick $V0 $H1:$B1/$V0 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_remove_brick_status_completed_field "$V0 $H1:$B1/$V0"

TEST $CLI_1 volume remove-brick $V0 $H1:$B1/$V0 commit

kill_glusterd 1

total_files=`find $M0 -name "files*" | wc -l`
TEST [ $total_files -eq 16 ];

TEST $CLI_2 volume add-brick $V0 $H3:$B3/$V0

TEST $CLI_2 volume rebalance $V0  start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status $V0

total_files=`find $M0 -name "files*" | wc -l`
TEST [ $total_files -eq 16 ];

TEST $CLI_2 volume add-brick $V0 $H4:$B4/$V0

TEST $CLI_2 volume rebalance $V0  start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status $V0
kill_glusterd 2

total_files=`find $M0 -name "files*" | wc -l`
TEST [ $total_files -eq 16 ];

cleanup;

