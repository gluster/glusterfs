#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test enables management ssl and then test the
# heal info command.

cleanup;
touch /var/lib/glusterd/secure-access
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^6$" ec_child_up_count $V0 0
touch $M0/a
TEST kill_brick $V0 $H0 $B0/${V0}5
echo abc > $M0/a
EXPECT_WITHIN  $HEAL_TIMEOUT "^5$" get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "^6$" ec_child_up_count $V0 0
TEST gluster volume heal $V0 info
EXPECT_WITHIN  $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0 #One for each active brick
cleanup;
