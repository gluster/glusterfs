#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc


cleanup;

# * How this test works ?
# 1. create a 3 node cluster
# 2. add them to trusted pool
# 3. create a volume and start
# 4. mount the volume with all 3 backup-volfile servers
# 5. kill glusterd in node 1
# 6. make changes to volume using node 2, using 'volume set' here
# 7. check whether those notifications are received by client

TEST launch_cluster 3;

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0

TEST $CLI_1 volume start $V0

TEST $CLI_1 volume status $V0;

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H1 --volfile-server=$H2 --volfile-server=$H3  $M0

TEST kill_glusterd 1

TEST $CLI_2 volume set $V0 performance.io-cache off

# make sure by this time directory will be created
# TODO: suggest ideal time to wait
sleep 5

count=$(find $M0/.meta/graphs/* -maxdepth 0 -type d -iname "*" | wc -l)
TEST [ "$count" -gt "1" ]

cleanup;
