#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;
mkdir -p $B0/1
mkdir -p $M0

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/1
TEST $CLI volume start $V0

# This is a pretty lame test.  Basically we just want to make sure that we
# get all the way through the translator stacks on client and server to get a
# simple error (95 = EOPNOTUPP) instead of a crash, RPC error, etc.
EXPECT 95 $PYTHON $(dirname $0)/ipctest.py $H0 $V0

cleanup;
