#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This command tests the volume create command validation for disperse volumes.

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 disperse 3 $H0:$B0/b4 $H0:$B0/b5 $H0:$B0/b6
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/b7 $H0:$B0/b8 $H0:$B0/b9
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 redundancy 1 $H0:$B0/b10 $H0:$B0/b11 $H0:$B0/b12
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 disperse-data 2 redundancy 1 $H0:$B0/b11 $H0:$B0/b12 $H0:$B0/b13
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 disperse-data 2 redundancy 1 $H0:$B0/b11 $H0:$B0/b12 $H0:$B0/b13
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 disperse 3 disperse-data 2 $H0:$B0/b14 $H0:$B0/b15 $H0:$B0/b16
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

TEST $CLI volume delete $V0
TEST $CLI volume create $V0 disperse 3 disperse-data 2 redundancy 1 $H0:$B0/b17 $H0:$B0/b18 $H0:$B0/b19
EXPECT "1 x \(2 \+ 1\) = 3" volinfo_field $V0 "Number of Bricks"

# -ve test cases
#Key-words appearing more than once
TEST ! $CLI volume create $V0 disperse 3 disperse 3 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 disperse-data 2 disperse-data 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 redundancy 1 redundancy 1 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22

#Minimum counts test
TEST ! $CLI volume create $V0 disperse 2 $H0:$B0/b20 $H0:$B0/b22
TEST ! $CLI volume create $V0 disperse-data 1 redundancy 0 $H0:$B0/b20 $H0:$B0/b22
TEST ! $CLI volume create $V0 redundancy 0 $H0:$B0/b20 $H0:$B0/b22

#Wrong count n != k+m
TEST ! $CLI volume create $V0 disperse 4 disperse-data 4 redundancy 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
#Num bricks is not multiple of disperse count
TEST ! $CLI volume create $V0 disperse 6 disperse-data 4 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
#Redundancy > data
TEST ! $CLI volume create $V0 disperse 6 disperse-data 2 redundancy 4 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 disperse 4 disperse-data 2 redundancy 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
#Replica + Disperse
TEST ! $CLI volume create $V0 disperse 4 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 disperse-data 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22 $H0:$B0/b23
TEST ! $CLI volume create $V0 redundancy 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 replica 2 disperse 4 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 replica 2 disperse-data 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22 $H0:$B0/b23
TEST ! $CLI volume create $V0 replica 2 redundancy 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
#Stripe + Disperse
TEST ! $CLI volume create $V0 disperse 4 stripe 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 disperse-data 2 stripe 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22 $H0:$B0/b23
TEST ! $CLI volume create $V0 redundancy 2 stripe 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 stripe 2 disperse 4 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 stripe 2 disperse-data 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22 $H0:$B0/b23
TEST ! $CLI volume create $V0 stripe 2 redundancy 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
#Stripe + Replicate + Disperse, It is failing with striped-dispersed volume.
TEST ! $CLI volume create $V0 disperse 4 stripe 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 disperse-data 2 stripe 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22 $H0:$B0/b23
TEST ! $CLI volume create $V0 redundancy 2 stripe 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 stripe 2 disperse 4 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
TEST ! $CLI volume create $V0 stripe 2 disperse-data 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22 $H0:$B0/b23
TEST ! $CLI volume create $V0 stripe 2 redundancy 2 replica 2 $H0:$B0/b20 $H0:$B0/b21 $H0:$B0/b22
cleanup
