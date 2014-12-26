#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting owner-uid as -12
TEST ! $CLI volume set $V0 owner-uid -12
EXPECT '' volinfo_field $V0 'storage.owner-uid'

## Setting owner-gid as -5
TEST ! $CLI volume set $V0 owner-gid -5
EXPECT '' volinfo_field $V0 'storage.owner-gid'

## Setting owner-uid as 36
TEST $CLI volume set $V0 owner-uid 36
EXPECT '36' volinfo_field $V0 'storage.owner-uid'

## Setting owner-gid as 36
TEST $CLI volume set $V0 owner-gid 36
EXPECT '36' volinfo_field $V0 'storage.owner-gid'

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
