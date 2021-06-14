#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

$CLI_1 volume create $V0 $H1:$B1/$V0  $H2:$B2/$V0
EXPECT 'Created' cluster_volinfo_field 1 $V0 'Status';

$CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

TEST glusterfs -s $H1 --volfile-id $V0 $M0;
TEST mkdir $M0/dir1

# Create a large file (4 GB), so that rebalance takes time
# Since we really don't care about the contents of the file, we use fallocate
# to generate the file much faster. We could also use truncate, which is even
# faster, but rebalance could take advantage of an sparse file and migrate it
# in an optimized way, but we don't want a fast migration.
TEST fallocate -l 4G $M0/dir1/foo

TEST mv $M0/dir1/foo $M0/dir1/bar

TEST $CLI_1 volume rebalance $V0 start force
TEST ! $CLI_1 volume set $V0 parallel-readdir on
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status_field 1 $V0
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status_field 2 $V0

# Setting readdir-ahead to ON, in order to satisfy the dependency chain
TEST $CLI_1 volume set $V0 readdir-ahead on
TEST $CLI_1 volume set $V0 parallel-readdir on
TEST mv $M0/dir1/bar $M0/dir1/foo

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H1 --volfile-id $V0 $M0;
TEST $CLI_1 volume rebalance $V0 start force
TEST ln $M0/dir1/foo $M0/dir1/bar
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status_field 1 $V0
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" cluster_rebalance_status_field 2 $V0
cleanup;
