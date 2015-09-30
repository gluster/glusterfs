#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 stripe 2 $H0:$B0/${V0}{1,2,3,4};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;

TEST glusterfs -s $H0 --volfile-id=$V0 $M0

## Test for symlink success
TEST touch $M0/reg_file
TEST ln -s $M0/reg_file $M0/symlink

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;

cleanup;
