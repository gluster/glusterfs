#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

TEST $CLI volume start $V0;

TEST $CLI volume barrier $V0 enable;

TEST ! $CLI volume barrier $V0 enable;

TEST $CLI volume barrier $V0 disable;

TEST ! $CLI volume barrier $V0 disable;

cleanup
