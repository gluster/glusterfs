#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This checks if the fop keeps the dirty flags settings correctly after
# finishing the fop.

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
cd $M0
for i in {1..1000}; do dd if=/dev/zero of=file-${i} bs=512k count=2; done
cd -
EXPECT "^0$" get_pending_heal_count $V0

cleanup
