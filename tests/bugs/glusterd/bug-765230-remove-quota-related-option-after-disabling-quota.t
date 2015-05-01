#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting quota-timeout as 20
TEST ! $CLI volume set $V0 features.quota-timeout 20
EXPECT '' volinfo_field $V0 'features.quota-timeout';

## Enabling features.quota-deem-statfs
TEST ! $CLI volume set $V0 features.quota-deem-statfs on
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

## Enabling quota
TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'

## Setting quota-timeout as 20
TEST $CLI volume set $V0 features.quota-timeout 20
EXPECT '20' volinfo_field $V0 'features.quota-timeout';

## Enabling features.quota-deem-statfs
TEST $CLI volume set $V0 features.quota-deem-statfs on
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

## Disabling quota
TEST $CLI volume quota $V0 disable
EXPECT 'off' volinfo_field $V0 'features.quota'
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'
EXPECT '' volinfo_field $V0 'features.quota-timeout'

## Setting quota-timeout as 30
TEST ! $CLI volume set $V0 features.quota-timeout 30
EXPECT '' volinfo_field $V0 'features.quota-timeout';

## Disabling features.quota-deem-statfs
TEST ! $CLI volume set $V0 features.quota-deem-statfs off
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

## Finish up
TEST $CLI volume stop $V0
EXPECT "1" get_aux
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
