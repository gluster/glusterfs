#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../cluster.rc

function get_volume_info ()
{
        local var=$1
        $CLI_1 volume info $V0 | grep "^$var" | sed 's/.*: //'
}

cleanup;

TEST verify_lvm_version
TEST launch_cluster 2
TEST setup_lvm 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2
EXPECT "$V0" get_volume_info 'Volume Name';
EXPECT 'Created' get_volume_info 'Status';

TEST $CLI_1 volume start $V0
EXPECT 'Started' get_volume_info 'Status';


# Setting system limit
TEST $CLI_1 snapshot config activate-on-create enable

TEST $CLI_1 snapshot create snap1 $V0 no-timestamp description "test"
TEST kill_glusterd 1
#deactivate snapshot for changing snap version, so that handshake will
#happen when glusterd is restarted
TEST $CLI_2 snapshot deactivate snap1
TEST start_glusterd 1

#Wait till handshake complete
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} 'Stopped' snapshot_status snap1

#Delete the snapshot, without this fix, delete will lead to assertion failure
$CLI_1 snapshot delete all
EXPECT '0' get_snap_count CLI_1;
cleanup;

