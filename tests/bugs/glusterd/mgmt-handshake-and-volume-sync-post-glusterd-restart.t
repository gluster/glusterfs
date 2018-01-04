#! /bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
$CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup

TEST launch_cluster 3

TEST $CLI_1 peer probe $H2

#bug-1109741 - validate mgmt handshake

TEST ! $CLI_3 peer probe $H1

GD1_WD=$($CLI_1 system getwd)
OP_VERS_ORIG=$(grep 'operating-version' ${GD1_WD}/glusterd.info | cut -d '=' -f 2)

TEST $CLI_3 system uuid get # Needed for glusterd.info to be created

GD3_WD=$($CLI_3 system getwd)
TEST sed -rnie "'s/(operating-version=)\w+/\130600/gip'" ${GD3_WD}/glusterd.info

TEST kill_glusterd 3
TEST start_glusterd 3

TEST ! $CLI_3 peer probe $H1

OP_VERS_NEW=$(grep 'operating-version' ${GD1_WD}/glusterd.info | cut -d '=' -f 2)
TEST [[ $OP_VERS_ORIG == $OP_VERS_NEW ]]

#bug-948686 - volume sync after bringing up the killed node

TEST $CLI_1 peer probe $H3
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
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers
TEST $CLI_3 volume start $V0;
TEST $CLI_2 volume stop $V0;
TEST $CLI_2 volume delete $V0;

cleanup
