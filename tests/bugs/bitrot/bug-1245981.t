#!/bin/bash

## Test case for bitrot
## Tunable object signing waiting time value for bitrot.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

SLEEP_TIME=5

cleanup;
## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create and start the volume
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
TEST $CLI volume set $V0 network.inode-lru-limit 1
## Enable bitrot on volume $V0
TEST $CLI volume bitrot $V0 enable

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

# wait a bit for oneshot crawler to finish
sleep 2;

## Set object expiry time value
TEST $CLI volume bitrot $V0 signing-time $SLEEP_TIME

## Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

# create and check object signature
fname="$M0/filezero"
echo "ZZZ" > $fname
echo "123" > $M0/new_file;

touch $M0/1
touch $M0/2
touch $M0/3
touch $M0/4
touch $M0/5

# wait till the object is signed
sleep `expr $SLEEP_TIME \* 2`

backpath=$(get_backend_paths $fname)
TEST getfattr -m . -n trusted.bit-rot.signature $backpath

backpath=$(get_backend_paths $M0/new_file)
TEST getfattr -m . -n trusted.bit-rot.signature $backpath

cleanup;
