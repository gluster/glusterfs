#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc


cleanup;
TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

$CLI_1 volume create $V0 $H1:$B1/$V0  $H2:$B2/$V0
EXPECT 'Created' cluster_volinfo_field 1 $V0 'Status';

$CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

$CLI_1 volume rebalance $V0  start &
#kill glusterd2 after requst sent, so that call back is called
#with rpc->status fail ,so roughly 1sec delay is introduced to get this scenario.
sleep 1
kill_glusterd 2
#check glusterd commands are working after rebalance start command
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

cleanup;
