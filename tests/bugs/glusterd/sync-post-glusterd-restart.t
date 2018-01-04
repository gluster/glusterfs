#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function volume_get_field()
{
     local vol=$1
     local field=$2
     $CLI_2 volume get $vol $field | tail -1 | awk '{print $2}'
}

cleanup

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

TEST $CLI_1 volume set $V0 performance.readdir-ahead on

# Bring down 2nd glusterd
TEST kill_glusterd 2

##bug-1420637 and bug-1323287 - sync post glusterd restart

TEST $CLI_1 volume set all cluster.server-quorum-ratio 60
TEST $CLI_1 volume set $V0 performance.readdir-ahead off
TEST $CLI_1 volume set $V0 performance.write-behind off

# Bring back 2nd glusterd
TEST $glusterd_2

# After 2nd glusterd come back, there will be 2 nodes in a cluster
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

#bug-1420637-volume sync post glusterd restart

EXPECT_WITHIN $PROBE_TIMEOUT "60" volinfo_field_2 all cluster.server-quorum-ratio
EXPECT_WITHIN $PROBE_TIMEOUT "off" volinfo_field_2 $V0 performance.readdir-ahead

#bug-1323287
EXPECT_WITHIN $PROBE_TIMEOUT 'off' volume_get_field $V0 'write-behind'

#bug-1213295 - volume stop should not crash glusterd post glusterd restart

TEST $CLI_2 volume stop $V0
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V1 $H1:$B1/$V1  $H2:$B2/$V1

cleanup
