#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc


cleanup;
V0="TestLongVolnamec363b7b536700ff06eedeae0dd9037fec363b7b536700ff06eedeae0dd9037fec363b7b536700ff06eedeae0dd9abcd"
V1="TestLongVolname3102bd28a16c49440bd5210e4ec4d5d93102bd28a16c49440bd5210e4ec4d5d933102bd28a16c49440bd5210e4ebbcd"
TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

$CLI_1 volume create $V0 $H1:$B1/$V0  $H2:$B2/$V0
EXPECT 'Created' cluster_volinfo_field 1 $V0 'Status';
$CLI_1 volume create $V1 $H1:$B1/$V1  $H2:$B2/$V1
EXPECT 'Created' cluster_volinfo_field 1 $V1 'Status';

$CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

$CLI_1 volume start $V1
EXPECT 'Started' cluster_volinfo_field 1 $V1 'Status';

#Mount FUSE
TEST glusterfs -s $H1 --volfile-id=$V0 $M0;


#Mount FUSE
TEST glusterfs -s $H1 --volfile-id=$V1 $M1;

TEST mkdir $M0/dir{1..4};
TEST touch $M0/dir{1..4}/files{1..4};

TEST mkdir $M1/dir{1..4};
TEST touch $M1/dir{1..4}/files{1..4};

TEST $CLI_1 volume add-brick $V0 $H1:$B1/${V0}_1 $H2:$B2/${V0}_1
TEST $CLI_1 volume add-brick $V1 $H1:$B1/${V1}_1 $H2:$B2/${V1}_1


TEST $CLI_1 volume rebalance $V0 start
TEST $CLI_1 volume rebalance $V1  start

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status_field 1 $V0
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status_field 1  $V1

cleanup;
