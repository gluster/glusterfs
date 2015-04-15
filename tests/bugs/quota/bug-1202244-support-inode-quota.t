#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function get_quota_value()
{
        local LIST_TYPE=$1
        local LIMIT_PATH=$2;
        $CLI volume quota $V0 $LIST_TYPE $LIMIT_PATH | grep "$LIMIT_PATH"\
                                                     | awk '{print $2}'
}

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0;
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

TEST $CLI volume quota $V0 enable;
EXPECT "on" volinfo_field $V0 'features.quota'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" quotad_up_status;

TEST mkdir $M0/dir;

TEST $CLI volume quota $V0 limit-usage /dir 10MB;
EXPECT "10.0MB" get_quota_value "list" "/dir"

TEST $CLI volume quota $V0 limit-objects /dir 10;
EXPECT "10" get_quota_value "list-objects" "/dir"

TEST $CLI volume quota $V0 remove /dir;
EXPECT "" get_quota_value "list" "/dir"

TEST $CLI volume quota $V0 remove-objects /dir;
EXPECT "" get_quota_value "list-objects" "/dir"

cleanup;
