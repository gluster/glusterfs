#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

# Disable self-heal-daemon, client-side-heal and set quorum-type to none
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.quorum-type none

#Kill bricks 0 & 1 and create a file to have pending entry for 0 & 1 on brick 2
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
echo "file 1" >> $M0/f1
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}2
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}2

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

#Kill bricks 1 & 2 and create a file to have pending entry for 1 & 2 on brick 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
echo "file 2" >> $M0/f2
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}0

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

#Kill bricks 2 & 0 and create a file to have pending entry for 2 & 0 on brick 1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}0
echo "file 3" >> $M0/f3
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}1

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

#Kill brick 0 and turn on the client side heal and do ls to trigger the heal.
#The pending xattrs on bricks 1 & 2 should have pending entry on brick 0.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on

TEST ls $M0
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}1
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}2
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}2

#Bring back all the bricks and trigger the heal again by doing ls. Now the
#pending xattrs on all the bricks should be 0.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST ls $M0

TEST cat $M0/f1
TEST cat $M0/f2
TEST cat $M0/f3

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}0
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}1
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}2
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}2

#Check whether all the bricks contains all the 3 files.
EXPECT "3" echo $(ls $B0/${V0}0 | wc -l)
EXPECT "3" echo $(ls $B0/${V0}1 | wc -l)
EXPECT "3" echo $(ls $B0/${V0}2 | wc -l)

cleanup;
