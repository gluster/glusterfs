#!/bin/bash

. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../include.rc

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

build_tester $(dirname $0)/bug-860663.c

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST $(dirname $0)/bug-860663 $M0/files 1000

ORIG_FILE_COUNT=`ls -l $M0 | wc -l`;
TEST [ $ORIG_FILE_COUNT -ge 1000 ]

# Kill a brick process
kill_brick $V0 $H0 $B0/${V0}1

TEST $CLI volume rebalance $V0 fix-layout start

EXPECT_WITHIN $REBALANCE_TIMEOUT "fix-layout failed" fix-layout_status_field $V0;

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

# Unmount and remount to make sure we're doing fresh lookups.
TEST umount $M0
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

NEW_FILE_COUNT=`ls -l $M0 | wc -l`;

EXPECT "0" file_count $ORIG_FILE_COUNT $NEW_FILE_COUNT

rm -f $(dirname $0)/bug-860663
cleanup;
