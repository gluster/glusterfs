#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

V1="patchy2"
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

function check_brick()
{
        vol=$1;
        num=$2
        $CLI volume info $V0 | grep "Brick$num" | awk '{print $2}';
}

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
EXPECT '2' brick_count $V0


EXPECT "$H0:$B0/${V0}1" check_brick $V0 '1';
EXPECT "$H0:$B0/${V0}2" check_brick $V0 '2';

TEST ! $CLI volume create $V1 $H0:$B0/${V1}0 $H0:$B0/${V0}1;

cleanup;
