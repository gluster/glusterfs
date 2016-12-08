#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../fdl.rc

cleanup;

TEST verify_lvm_version;
#Create cluster with 3 nodes
TEST launch_cluster 3;
TEST setup_lvm 3

TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count;

TEST $CLI_1 volume create $V0 replica 3 $H1:$L1 $H2:$L2 $H3:$L3
TEST $CLI_1 volume set $V0 cluster.jbr on
TEST $CLI_1 volume set $V0 cluster.jbr.quorum-percent 100
TEST $CLI_1 volume set $V0 features.fdl on
#TEST $CLI_1 volume set $V0 diagnostics.brick-log-level DEBUG
TEST $CLI_1 volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H1 --entry-timeout=0 $M0;

EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" jbrc_child_up_status $V0 0

echo "file" > $M0/file1
TEST stat $L1/file1
TEST stat $L2/file1
TEST stat $L3/file1

cleanup;
#G_TESTDEF_TEST_STATUS_CENTOS6=KNOWN_ISSUE,BUG=1385758
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=1385758
