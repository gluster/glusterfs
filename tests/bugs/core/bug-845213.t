#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;


## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Create and start a volume with aio enabled
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 remote-dio enable;
TEST $CLI volume set $V0 network.remote-dio disable;

cleanup;

