#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks optimistic-change-log option

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume heal $V0 disable

TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 disperse.background-heals 0
TEST $CLI volume set $V0 disperse.optimistic-change-log off
TEST $CLI volume set $V0 disperse.eager-lock off
TEST $CLI volume set $V0 disperse.other-eager-lock off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength

TEST $CLI volume set $V0 disperse.background-heals 1
TEST touch $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" count_sh_entries $B0/${V0}0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" count_sh_entries $B0/${V0}1
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" count_sh_entries $B0/${V0}2



### optimistic-change-log = off ; All bricks good. Test file operation
echo abc > $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = off ; Kill one brick . Test file operation
TEST kill_brick $V0 $H0 $B0/${V0}2
echo abc > $M0/a
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
#Accessing file should heal the file now
EXPECT "abc" cat $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = off ; All bricks good. Test entry operation
TEST touch $M0/b
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = off ; All bricks good. Test metadata operation
TEST chmod 0777 $M0/b
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = off ; Kill one brick. Test entry operation

TEST kill_brick $V0 $H0 $B0/${V0}2
TEST touch $M0/c
EXPECT 4 get_pending_heal_count $V0 #two for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
getfattr -d -m. -e hex $M0 2>&1 > /dev/null
getfattr -d -m. -e hex $M0/c 2>&1 > /dev/null
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = off ; Kill one brick. Test metadata operation
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST chmod 0777 $M0/c
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
getfattr -d -m. -e hex $M0/c 2>&1 > /dev/null
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

TEST $CLI volume set $V0 disperse.optimistic-change-log on

### optimistic-change-log = on ; All bricks good. Test file operation

echo abc > $M0/aa
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = on ; Kill one brick. Test file operation

TEST kill_brick $V0 $H0 $B0/${V0}2
echo abc > $M0/aa
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
#Accessing file should heal the file now
getfattr -d -m. -e hex $M0/aa 2>&1 > /dev/null
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = on ; All bricks good. Test entry operation

TEST touch $M0/bb
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = on ; All bricks good. Test metadata operation

TEST chmod 0777 $M0/bb
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = on ; Kill one brick. Test entry operation

TEST kill_brick $V0 $H0 $B0/${V0}2
TEST touch $M0/cc
EXPECT 4 get_pending_heal_count $V0 #two for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
getfattr -d -m. -e hex $M0 2>&1 > /dev/null
getfattr -d -m. -e hex $M0/cc 2>&1 > /dev/null
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

## optimistic-change-log = on ; Kill one brick. Test metadata operation

TEST kill_brick $V0 $H0 $B0/${V0}2
TEST chmod 0777 $M0/cc
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
getfattr -d -m. -e hex $M0/cc 2>&1 > /dev/null
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

############################################################

cleanup
