#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

STR="1234567890"
volname="Vol"

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;


# Construct volname string such that its more than 256 characters
for i in {1..30}
do
    volname+=$STR
done
# Now $volname is more than 256 chars

TEST ! $CLI volume create $volname $H0:$B0/${volname}{1,2};

TEST $CLI volume info;

# Construct brick string such that its more than 256 characters
volname="Vol1234"
brick="brick"
for i in {1..30}
do
    brick+=$STR
done
# Now $brick1 is more than 256 chars

TEST ! $CLI volume create $volname $H0:$B0/$brick;

TEST $CLI volume info;

# Now try to create a volume with couple of bricks (strlen(volname) = 128 &
# strlen(brick1) = 128
# Command should still fail as strlen(volp path) > 256

volname="Volume-0"
brick="brick-00"
STR="12345678"

for i in {1..15}
do
    volname+=$STR
    brick+=$STR
done
TEST ! $CLI volume create $volname $H0:$B0/$brick;

TEST $CLI volume info;

# test case with brick path as 255 and a trailing "/"
brick=""
STR1="12345678"
volname="vol"

for i in {1..31}
do
    brick+=$STR1
done
brick+="123456/"

echo $brick | wc -c
# Now $brick is exactly 255 chars, but at end a trailing space
# This will still fail as volfpath exceeds more than _POSIX_MAX chars

TEST ! $CLI volume create $volname $H0:$B0/$brick;

TEST $CLI volume info;

# Positive test case
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

TEST $CLI volume info;

cleanup;
