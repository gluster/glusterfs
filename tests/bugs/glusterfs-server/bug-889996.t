#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

rm -rf $B0/${V0}1;

TEST ! $CLI volume start $V0;
EXPECT 0 online_brick_count;

cleanup;
