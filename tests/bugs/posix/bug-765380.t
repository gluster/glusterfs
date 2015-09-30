#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd

REPLICA=2

TEST $CLI volume create $V0 replica $REPLICA $H0:$B0/${V0}00 $H0:$B0/${V0}01 $H0:$B0/${V0}10 $H0:$B0/${V0}11
TEST $CLI volume start $V0

## Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0;

function count_hostname_or_uuid_from_pathinfo()
{
    pathinfo=$(getfattr -n trusted.glusterfs.pathinfo $M0/f00f)
    echo $pathinfo | grep -o $1 | wc -l
}

TEST touch $M0/f00f

EXPECT $REPLICA count_hostname_or_uuid_from_pathinfo $H0

# turn on node-uuid-pathinfo option
TEST $CLI volume set $V0 node-uuid-pathinfo on

# do not expext hostname as part of the pathinfo string
EXPECT 0 count_hostname_or_uuid_from_pathinfo $H0

uuid=$(grep UUID $GLUSTERD_WORKDIR/glusterd.info | cut -f2 -d=)

# ... but expect the uuid $REPLICA times
EXPECT $REPLICA count_hostname_or_uuid_from_pathinfo $uuid

cleanup;
