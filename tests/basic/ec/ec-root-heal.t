#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup
function num_entries {
        ls -l $1 | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
touch $M0/{1..10}
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}5 $H0:$B0/${V0}6 commit force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count_shd $V0 0

# active heal
TEST $CLI volume heal $V0 full
#ls -l gives "Total" line so number of lines will be 1 more
EXPECT_WITHIN $HEAL_TIMEOUT "^11$" num_entries $B0/${V0}6
ec_version=$(get_hex_xattr trusted.ec.version $B0/${V0}0)
EXPECT "$ec_version" get_hex_xattr trusted.ec.version $B0/${V0}1
EXPECT "$ec_version" get_hex_xattr trusted.ec.version $B0/${V0}2
EXPECT "$ec_version" get_hex_xattr trusted.ec.version $B0/${V0}3
EXPECT "$ec_version" get_hex_xattr trusted.ec.version $B0/${V0}4
EXPECT "$ec_version" get_hex_xattr trusted.ec.version $B0/${V0}6

cleanup;
