#!/bin/bash

#Testcase:
#On snapshot, notify changelog reconfigure upon explicit rollover
#irrespective of any failures and send error back to barrier if any.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../snapshot.rc

cleanup;
TEST verify_lvm_version
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

## Create a volume
TEST $CLI volume create $V0 $H0:$L1

## Start volume and verify
TEST $CLI volume start $V0

TEST $CLI volume set $V0 changelog.changelog on
##Wait for changelog init to complete.
sleep 1

## Take snapshot
TEST $CLI snapshot create snap1 $V0

cleanup;
