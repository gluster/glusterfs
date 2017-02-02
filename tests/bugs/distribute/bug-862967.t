#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function uid_gid_compare()
{
        val=1

        if [ "$1" == "$3" ]
        then
                if [ "$2" == "$4" ]
                        then
                                val=0
                fi
        fi
        echo "$val"
}

BRICK_COUNT=3

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs --attribute-timeout=0 --entry-timeout=0 --gid-timeout=-1 -s $H0 --volfile-id $V0 $M0;

# change dir permissions
mkdir $M0/dir;
chown 1:1 $M0/dir;

# Kill a brick process

kill_brick $V0 $H0 $B0/${V0}1
# change dir ownership
NEW_UID=36;
NEW_GID=36;
chown $NEW_UID:$NEW_GID $M0/dir;

# bring the brick back up
TEST $CLI volume start $V0 force

sleep 10;

ls -l $M0/dir;

# check if uid/gid is healed on backend brick which was taken down
BACKEND_UID=`stat -c %u $B0/${V0}1/dir`;
BACKEND_GID=`stat -c %g $B0/${V0}1/dir`;


EXPECT "0" uid_gid_compare $NEW_UID $NEW_GID $BACKEND_UID $BACKEND_GID

cleanup;
