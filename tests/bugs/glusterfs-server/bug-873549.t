#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd -LDEBUG;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

TEST $CLI volume set $V0 performance.cache-size 512MB
TEST $CLI volume start $V0
TEST $CLI volume statedump $V0 all

cleanup;
