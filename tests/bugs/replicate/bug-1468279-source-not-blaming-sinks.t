#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 --attribute-timeout=0 --entry-timeout=0 $M0;
TEST touch $M0/file

# Kill B1, create a pending metadata heal.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST setfattr -n user.xattr -v value1 $M0/file
EXPECT "0000000000000010000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1/file
EXPECT "0000000000000010000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}2/file

# Kill B2, heal from B3 to B1.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
$CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT  "00000000" afr_get_specific_changelog_xattr $B0/${V0}2/file trusted.afr.$V0-client-0 "metadata"
TEST $CLI volume set $V0 cluster.self-heal-daemon off

# Create another pending metadata heal.
TEST setfattr -n user.xattr -v value2 $M0/file
EXPECT "0000000000000010000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0/file
EXPECT "0000000000000010000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}2/file

# Kill B1, heal from B3 to B2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
$CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT  "00000000" afr_get_specific_changelog_xattr $B0/${V0}2/file trusted.afr.$V0-client-1 "metadata"
TEST $CLI volume set $V0 cluster.self-heal-daemon off

# ALL bricks up again.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
# B1 and B2 blame each other, B3 doesn't blame anyone.
EXPECT "0000000000000010000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0/file
EXPECT "0000000000000010000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1/file
EXPECT "0000000000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}2/file
EXPECT "0000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}2/file
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

cleanup;
