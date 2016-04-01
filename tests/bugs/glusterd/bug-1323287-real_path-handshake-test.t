#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc

function volume_get_field()
{
     local vol=$1
     local field=$2
     $CLI_2 volume get $vol $field | tail -1 | awk '{print $2}'
}

cleanup;
TEST launch_cluster 2;
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0  $H2:$B2/$V0
EXPECT 'Created' cluster_volinfo_field 1 $V0 'Status';

TEST $CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

#kill glusterd2 and do a volume set command to change the version
kill_glusterd 2

TEST $CLI_1 volume set $V0 performance.write-behind off
TEST start_glusterd 2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

#Check for handshake completion.
EXPECT_WITHIN $PROBE_TIMEOUT 'off' volume_get_field $V0 'write-behind'

#During hanndshake, if we failed to populate real_path,
#then volume create will fail.
TEST $CLI_1 volume create $V1 $H1:$B1/$V1  $H2:$B2/$V1
