#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

function get_brick_host_uuid()
{
    local vol=$1;
    local uuid_regex='[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}'
    local host_uuid_list=$($CLI volume info $vol --xml | grep "brick.uuid" | grep -o -E "$uuid_regex");

    echo $host_uuid_list | awk '{print $1}'
}

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}

uuid=`grep UUID $GLUSTERD_WORKDIR/glusterd.info | cut -f2 -d=`
EXPECT $uuid get_brick_host_uuid $V0

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
