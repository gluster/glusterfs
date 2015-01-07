#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3;

function brick_count()
{
    local vol=$1;

    $CLI volume info $vol | egrep "^Brick[0-9]+: " | wc -l;
}


TEST $CLI volume start $V0
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 force;
EXPECT '2' brick_count $V0

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}3 force;
EXPECT '1' brick_count $V0

cleanup;

