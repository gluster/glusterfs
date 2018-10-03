#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc

function peer_count {
eval \$CLI_$1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

#bug-1454418 -  Setting Port number in specific range
sysctl net.ipv4.ip_local_reserved_ports="24007-24008,32765-32768,49152-49156"

TEST launch_cluster 4;

#bug-1223213

# Fool the cluster to operate with 3.5 version even though binary's op-version
# is > 3.5. This is to ensure 3.5 code path is hit to test that volume status
# works when a node is upgraded from 3.5 to 3.7 or higher as mgmt_v3 lock is
# been introduced in 3.6 version and onwards

GD1_WD=$($CLI_1 system getwd)
$CLI_1 system uuid get
Old_op_version=$(cat ${GD1_WD}/glusterd.info | grep operating-version | cut -d '=' -f 2)

TEST sed -rnie "'s/(operating-version=)\w+/\130500/gip'" ${GD1_WD}/glusterd.info

TEST kill_glusterd 1
TEST start_glusterd 1

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 1

TEST `sed -i "s/"30500"/${Old_op_version}/g" ${GD1_WD}/glusterd.info`

TEST kill_glusterd 1
TEST start_glusterd 1

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 1
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 2

#bug-1454418
sysctl net.ipv4.ip_local_reserved_ports="
"

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

#bug-888752 - volume status --xml from peer in the cluster

TEST $CLI_1 volume status $V0 $H2:$B2/$V0 --xml

TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume delete $V0

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume create $V1 $H1:$B1/$V1

# bug - 1635820
# rebooting a node which doen't host bricks for any one volume
# peer should not go into rejected state
TEST kill_glusterd 2
TEST start_glusterd 2

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 1
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 2

TEST $CLI_1 volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field_1 $V0 'Status'

TEST $CLI_1 volume start $V1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field_1 $V1 'Status'

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1

TEST $CLI_1 peer probe $H4;
EXPECT_WITHIN $PROBE_TIMEOUT 3 peer_count 1

#bug-1173414 - validate mgmt-v3-remote-lock-failure

for i in {1..20}
do
$CLI_1 volume set $V0 diagnostics.client-log-level DEBUG &
$CLI_1 volume set $V1 barrier on
$CLI_2 volume set $V0 diagnostics.client-log-level DEBUG &
$CLI_2 volume set $V1 barrier on
done

EXPECT_WITHIN $PROBE_TIMEOUT 3 peer_count 1
TEST $CLI_1 volume status
TEST $CLI_2 volume status

#bug-1293414 - validate peer detach

# peers hosting bricks cannot be detached
TEST ! $CLI_4 peer detach $H1
EXPECT_WITHIN $PROBE_TIMEOUT 3 peer_count 1

# peer not hosting bricks should be detachable
TEST $CLI_4 peer detach $H3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1

#bug-1344407 - deleting a volume when peer is down should fail

#volume should be stopped before deletion
TEST $CLI_1 volume stop $V0

TEST kill_glusterd 2
TEST ! $CLI_1 volume delete $V0

cleanup
