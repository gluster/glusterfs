#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function test_volume_config()
{
        volname=$1
        type_string=$2
        brickCount=$3
        distCount=$4
        replicaCount=$5
        arbiterCount=$6
        disperseCount=$7
        redundancyCount=$8

        EXPECT "$type_string" volinfo_field $volname "Number of Bricks"
        EXPECT "$brickCount" get-xml "volume info $volname" "brickCount"
        EXPECT "$distCount" get-xml "volume info $volname" "distCount"
        EXPECT "$replicaCount" get-xml "volume info $volname" "replicaCount"
        EXPECT "$arbiterCount" get-xml "volume info $volname" "arbiterCount"
        EXPECT "$disperseCount" get-xml "volume info $volname" "disperseCount"
        EXPECT "$redundancyCount" get-xml "volume info $volname" "redundancyCount"
}

# This command tests the volume create command and number of bricks for different volume types.
cleanup;
TESTS_EXPECTED_IN_LOOP=56
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create ${V0}_1 replica 3 arbiter 1 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
test_volume_config "${V0}_1" "1 x \(2 \+ 1\) = 3" "3" "1" "3" "1" "0" "0"

TEST $CLI volume create ${V0}_2 replica 3 arbiter 1 $H0:$B0/b{4..9}
test_volume_config "${V0}_2" "2 x \(2 \+ 1\) = 6" "6" "2" "3" "1" "0" "0"


TEST $CLI volume create ${V0}_3 replica 3 arbiter 1 $H0:$B0/b{10..12}
test_volume_config "${V0}_3" "1 x \(2 \+ 1\) = 3" "3" "1" "3" "1" "0" "0"
TEST killall -15 glusterd
TEST glusterd
TEST pidof glusterd
test_volume_config "${V0}_3" "1 x \(2 \+ 1\) = 3" "3" "1" "3" "1" "0" "0"

TEST $CLI volume create ${V0}_4 replica 3 $H0:$B0/b{13..15}
test_volume_config "${V0}_4" "1 x 3 = 3" "3" "1" "3" "0" "0" "0"

TEST $CLI volume create ${V0}_5 replica 3 $H0:$B0/b{16..21}
test_volume_config "${V0}_5" "2 x 3 = 6" "6" "2" "3" "0" "0" "0"

TEST $CLI volume create ${V0}_6 disperse 3 redundancy 1 $H0:$B0/b{22..24}
test_volume_config "${V0}_6" "1 x \(2 \+ 1\) = 3" "3" "1" "1" "0" "3" "1"

TEST $CLI volume create ${V0}_7 disperse 3 redundancy 1 $H0:$B0/b{25..30}
test_volume_config "${V0}_7" "2 x \(2 \+ 1\) = 6" "6" "2" "1" "0" "3" "1"

TEST $CLI volume create ${V0}_8 $H0:$B0/b{31..33}
test_volume_config "${V0}_8" "3" "3" "3" "1" "0" "0" "0"

cleanup
