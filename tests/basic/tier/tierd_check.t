#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{1..3}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume start $V0
        TEST $CLI volume attach-tier $V0 $H0:$B0/hot/${V0}{1..2}
        TEST $CLI volume set $V0 cluster.tier-mode test
}

function tier_deamon_kill () {
pkill -f "rebalance/$V0"
echo "$?"
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume status


#Create and start a tiered volume
create_dist_tier_vol

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 tier_daemon_check

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 tier_deamon_kill

TEST $CLI volume tier $V0 start

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_deamon_kill

TEST $CLI volume tier $V0 start force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check

TEST $CLI volume tier $V0 start force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check

# To test fordetach start fail while the brick is down

TEST pkill -f "$B0/hot/$V0"

TEST ! $CLI volume tier $V0 detach start

cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
