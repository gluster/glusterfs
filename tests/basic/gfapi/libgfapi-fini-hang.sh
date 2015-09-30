#!/bin/bash

. $(dirname $0)/../../include.rc

function check_process () {
    pgrep libgfapi-fini-hang
    if [ $? -eq 1 ] ; then
        echo "Y"
    else
        echo "N"
    fi
}

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

build_tester -lgfapi $(dirname $0)/libgfapi-fini-hang.c -o $M0/libgfapi-fini-hang
TEST cd $M0
 ./libgfapi-fini-hang $V0 &
lpid=$!

# check if the process "libgfapi-fini-hang" exits with in $PROCESS_UP_TIMEOUT
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Y' check_process

# Kill the process if present
TEST ! kill -9 $lpid

TEST rm -f $M0/libgfapi-fini-hang

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
