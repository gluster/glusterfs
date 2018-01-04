#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc


cleanup;

TEST verify_lvm_version
TEST launch_cluster 3;
TEST setup_lvm 3;

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 replica 2 $H1:$L1 $H2:$L2
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

#bug-1318591 - skip-non-directories-inside-vols

b="B1"
TEST touch ${!b}/glusterd/vols/file

TEST $CLI_1 snapshot create snap1 $V0 no-timestamp;

TEST touch ${!b}/glusterd/snaps/snap1/file

#bug-1322145 - peer hosting snapshotted bricks should not be detachable

kill_glusterd 2

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume replace-brick $V0 $H2:$L2 $H3:$L3 commit force

# peer hosting snapshotted bricks should not be detachable
TEST ! $CLI_1 peer detach $H2

TEST killall_gluster
TEST $glusterd_1
TEST $glusterd_2

cleanup;

