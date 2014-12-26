#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
    $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}
cleanup;
#setup cluster and test volume
TEST launch_cluster 3; # start 3-node virtual cluster
TEST $CLI_1 peer probe $H2; # peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H3; # peer probe server 3 from server 1 cli

EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers;

TEST $CLI_1 volume create $V0 replica 2 $H1:$B1/$V0 $H1:$B1/${V0}_1 $H2:$B2/$V0 $H3:$B3/$V0
TEST $CLI_1 volume start $V0
TEST glusterfs --volfile-server=$H1 --volfile-id=$V0 $M0

#kill a node
TEST kill_node 3

#modify volume config to see change in volume-sync
TEST $CLI_1 volume set $V0 write-behind off
#add some files to the volume to see effect of volume-heal cmd
TEST touch $M0/{1..100};
TEST $CLI_1 volume stop $V0;
TEST $glusterd_3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers;
TEST $CLI_3 volume start $V0;
TEST $CLI_2 volume stop $V0;
TEST $CLI_2 volume delete $V0;

cleanup;

TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
pkill glusterd;
pkill glusterfsd;
TEST glusterd
TEST $CLI volume status $V0

cleanup;
