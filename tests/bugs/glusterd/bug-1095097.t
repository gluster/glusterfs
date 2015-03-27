#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B1/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume profile $V0 start
TEST $CLI volume profile $V0 info

cleanup;
