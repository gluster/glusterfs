#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

function volume_count {
        local cli=$1;
        if [ $cli -eq '1' ] ; then
                $CLI_1 volume info | grep 'Volume Name' | wc -l;
        else
                $CLI_2 volume info | grep 'Volume Name' | wc -l;
        fi
}

cleanup;

TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

b="B1";

#Create an extra file in the originator's volume store
touch ${!b}/glusterd/vols/$V0/run/file

TEST $CLI_1 volume stop $V0
#Test for self-commit failure
TEST $CLI_1 volume delete $V0

#Check whether delete succeeded on both the nodes
EXPECT "0" volume_count '1'
EXPECT "0" volume_count '2'

#Check whether the volume name can be reused after deletion
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}1 $H2:$B2/${V0}1
TEST $CLI_1 volume start $V0

#Create an extra file in the peer's volume store
touch ${!b}/glusterd/vols/$V0/run/file

TEST $CLI_1 volume stop $V0
#Test for commit failure on the other node
TEST $CLI_2 volume delete $V0

EXPECT "0" volume_count '1';
EXPECT "0" volume_count '2';

cleanup;
