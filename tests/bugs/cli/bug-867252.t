#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}1;


function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


function brick_count()
{
    local vol=$1;

    $CLI volume info $vol | egrep "^Brick[0-9]+: " | wc -l;
}


EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '1' brick_count $V0

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}2;
EXPECT '2' brick_count $V0

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 force;
EXPECT '1' brick_count $V0

cleanup;
