#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


NUM_BRICKS=3
DEMOTE_FREQ=5
PROMOTE_FREQ=5



# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 disperse 6 disperse-data 4 $H0:$B0/cold/${V0}{1..12}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume set $V0 features.ctr-enabled on
        TEST $CLI volume start $V0
        TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/hot/${V0}{0..$1}
        TEST $CLI volume set $V0 cluster.tier-demote-frequency $DEMOTE_FREQ
        TEST $CLI volume set $V0 cluster.tier-promote-frequency $PROMOTE_FREQ
        TEST $CLI volume set $V0 cluster.read-freq-threshold 0
        TEST $CLI volume set $V0 cluster.write-freq-threshold 0
        TEST $CLI volume set $V0 cluster.tier-mode test
}

function tier_task_name () {
        local task_name=$1;
        $CLI volume status $V0 task | grep "$task_name";
        echo $?;
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info


#Create and start a tiered volume
create_dist_tier_vol $NUM_BRICKS

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST touch /mnt/glusterfs/0/file{1..100};

EXPECT "0" tier_task_name "Tier migration";

TEST $CLI volume tier $V0 detach start

EXPECT "0" tier_task_name "Detach tier";

TEST $CLI volume stop $V0 force;

TEST $CLI volume start $V0 force;

EXPECT "0" tier_task_name "Detach tier";

TEST $CLI volume tier $V0 detach stop

EXPECT "0" tier_task_name "Tier migration";

cleanup;
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
