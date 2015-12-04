#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 disperse 6 disperse-data 4 $H0:$B0/cold/${V0}{1..12}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume start $V0
        TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/hot/${V0}{0..5}
        TEST $CLI volume set $V0 cluster.tier-mode test
}

function tier_detach_commit () {
	$CLI volume tier $V0 detach commit | grep "success" | wc -l
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume status


#Create and start a tiered volume
create_dist_tier_vol

#Issue detach tier on the tiered volume
#Will throw error saying detach tier not started

EXPECT "Tier command failed" $CLI volume tier $V0 detach status

#after starting detach tier the detach tier status should display the status

TEST $CLI volume tier $V0 detach start

TEST $CLI volume tier $V0 detach status

TEST $CLI volume tier $V0 detach stop

#If detach tier is stopped the detach tier command will fail

EXPECT "Tier command failed" $CLI volume tier $V0 detach status

TEST $CLI volume tier $V0 detach start

#wait for the detach to complete
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" tier_detach_commit

#If detach tier is committed then the detach status should fail throwing an error
#saying its not a tiered volume

EXPECT "Tier command failed" $CLI volume tier $V0 detach status

cleanup;

