#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks';

TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}2
EXPECT '1 x 3 = 3' volinfo_field $V0 'Number of Bricks';

TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}1 force
EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks';

TEST killall glusterd
TEST glusterd

EXPECT '1 x 2 = 2' volinfo_field $V0 'Number of Bricks';
cleanup
