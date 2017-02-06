#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks background heals option

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
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
TEST touch $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" count_sh_entries $B0/${V0}0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" count_sh_entries $B0/${V0}1
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" count_sh_entries $B0/${V0}2

TEST kill_brick $V0 $H0 $B0/${V0}2
echo abc > $M0/a
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
#Accessing file shouldn't heal the file
EXPECT "abc" cat $M0/a
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
TEST $CLI volume set $V0 disperse.background-heals 1
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "128" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
#Accessing file should heal the file now
EXPECT "abc" cat $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#Test above test cases with reset instead of setting background-heals to 1
TEST $CLI volume set $V0 disperse.heal-wait-qlength 1024
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1024" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
TEST $CLI volume set $V0 disperse.background-heals 0
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
TEST $CLI volume set $V0 disperse.heal-wait-qlength 200 #Changing qlength shouldn't affect anything now
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
TEST kill_brick $V0 $H0 $B0/${V0}2
echo abc > $M0/a
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
$CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
#Accessing file shouldn't heal the file
EXPECT "abc" cat $M0/a
sleep 3
EXPECT 2 get_pending_heal_count $V0 #One for each active brick
TEST $CLI volume reset $V0 disperse.background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "8" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "200" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
#Accessing file should heal the file now
EXPECT "abc" cat $M0/a
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#Test that disabling background-heals still drains the queue
TEST $CLI volume set $V0 disperse.background-heals 1
TEST touch $M0/{a,b,c,d}
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "200" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
TEST truncate -s 1GB $M0/a
echo abc > $M0/b
echo abc > $M0/c
echo abc > $M0/d
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST chown root:root $M0/{a,b,c,d}
TEST $CLI volume set $V0 disperse.background-heals 0
EXPECT_NOT "0" mount_get_option_value $M0 $V0-disperse-0 heal-waiters
TEST truncate -s 0 $M0/a # This completes the heal fast ;-)
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

#Test that background heals get rejected on meeting background-qlen limit
TEST $CLI volume set $V0 disperse.background-heals 1
TEST $CLI volume set $V0 disperse.heal-wait-qlength 0
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "1" mount_get_option_value $M0 $V0-disperse-0 background-heals
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-disperse-0 heal-wait-qlength
TEST truncate -s 1GB $M0/a
echo abc > $M0/b
echo abc > $M0/c
echo abc > $M0/d
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST chown root:root $M0/{a,b,c,d}
EXPECT "0" mount_get_option_value $M0 $V0-disperse-0 heal-waiters
cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1419696
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1419696
