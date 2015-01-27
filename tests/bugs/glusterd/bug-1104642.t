#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc


function get_value()
{
        local key=$1
        local var="CLI_$2"

        eval cli_index=\$$var

        $cli_index volume info | grep "^$key"\
                               | sed 's/.*: //'
}

cleanup

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/${V0}0 $H2:$B2/${V0}1
EXPECT "$V0" get_value 'Volume Name' 1
EXPECT "Created" get_value 'Status' 1

TEST $CLI_1 volume start $V0
EXPECT "Started" get_value 'Status' 1

#Bring down 2nd glusterd
TEST kill_glusterd 2

#set the volume all options from the 1st glusterd
TEST $CLI_1 volume set all cluster.server-quorum-ratio 80

#Bring back the 2nd glusterd
TEST $glusterd_2

#Verify whether the value has been synced
EXPECT '80' get_value 'cluster.server-quorum-ratio' 1
EXPECT_WITHIN $PROBE_TIMEOUT '1' peer_count
EXPECT_WITHIN $PROBE_TIMEOUT '80' get_value 'cluster.server-quorum-ratio' 2

cleanup;
