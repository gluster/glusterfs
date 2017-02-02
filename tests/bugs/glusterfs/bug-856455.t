#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

BRICK_COUNT=3

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0;

function query_pathinfo()
{
    local path=$1;
    local retval;

    local pathinfo=$(getfattr  -n trusted.glusterfs.pathinfo $path);
    retval=$(echo $pathinfo | grep -o 'POSIX' | wc -l);
    echo $retval
}

TEST touch $M0/f00f;
TEST mkdir $M0/f00d;

# verify pathinfo for a file and directory
EXPECT 1 query_pathinfo $M0/f00f;
EXPECT $BRICK_COUNT query_pathinfo $M0/f00d;

# Kill a brick process and then query for pathinfo
# for directories pathinfo should list backend patch from available (up) subvolumes

kill_brick $V0 $H0 ${B0}/${V0}1

EXPECT `expr $BRICK_COUNT - 1` query_pathinfo $M0/f00d;

cleanup;
