#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

function file_count()
{
        val=1

        if [ "$1" == "$2" ]
        then
                val=0
        fi
        echo $val
}

BRICK_COUNT=3

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;
sleep 5;

TEST touch $M0/files{1..10000};

ORIG_FILE_COUNT=`ls -l $M0 | wc -l`;

# Kill a brick process
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-${V0}1.pid`;

TEST $CLI volume rebalance $V0 fix-layout start

sleep 30;

TEST ! touch $M0/files{1..10000};

TEST $CLI volume start $V0 force

sleep 5;

NEW_FILE_COUNT=`ls -l $M0 | wc -l`;

EXPECT "0" file_count $ORIG_FILE_COUNT $NEW_FILE_COUNT

cleanup;
