#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This script checks if hardlinks that are created while a brick is down are
#healed properly.

cleanup;
function get_inum {
        ls -i $1 | awk '{print $1}'
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/a
TEST ln $M0/a $M0/link_a
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST ls -l $M0
inum=$(get_inum $B0/${V0}0/a)
EXPECT "$inum" get_inum $B0/${V0}0/link_a
cleanup
