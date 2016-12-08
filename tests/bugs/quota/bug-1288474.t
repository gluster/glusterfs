#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc

NUM_BRICKS=2

function create_dist_tier_vol () {
        mkdir -p $B0/cold/${V0}{0..$1}
        mkdir -p $B0/hot/${V0}{0..$1}
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{0..$1}
	TEST $CLI volume set $V0 nfs.disable false
        TEST $CLI volume start $V0
        TEST $CLI volume tier $V0 attach $H0:$B0/hot/${V0}{0..$1}
}

cleanup;

#Basic checks
TEST glusterd

#Create and start a tiered volume
create_dist_tier_vol $NUM_BRICKS

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
touch $M0/foobar

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 10MB

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quota_list_field "/" 5

#check quota list after detach tier
TEST $CLI volume detach-tier $V0 start
sleep 1
TEST $CLI volume detach-tier $V0 force

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quota_list_field "/" 5

#check quota list after attach tier
rm -rf $B0/hot
mkdir $B0/hot
TEST $CLI volume tier $V0 attach $H0:$B0/hot/${V0}{0..$1}

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "10.0MB" quota_list_field "/" 5

TEST umount $M0

cleanup;

