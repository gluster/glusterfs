#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a distribute-replicate volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Distributed-Replicate' volinfo_field $V0 'Type';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting stripe-block-size as 10MB
TEST ! $CLI volume set $V0 stripe-block-size 10MB
EXPECT '' volinfo_field $V0 'cluster.stripe-block-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a replicate volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 8 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Replicate' volinfo_field $V0 'Type';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting stripe-block-size as 10MB
TEST ! $CLI volume set $V0 stripe-block-size 10MB
EXPECT '' volinfo_field $V0 'cluster.stripe-block-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a distribute volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Distribute' volinfo_field $V0 'Type';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting stripe-block-size as 10MB
TEST ! $CLI volume set $V0 stripe-block-size 10MB
EXPECT '' volinfo_field $V0 'cluster.stripe-block-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a stripe volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 stripe 8 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Stripe' volinfo_field $V0 'Type';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting stripe-block-size as 10MB
TEST $CLI volume set $V0 stripe-block-size 10MB
EXPECT '10MB' volinfo_field $V0 'cluster.stripe-block-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a distributed stripe volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 stripe 4 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Distributed-Stripe' volinfo_field $V0 'Type';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting stripe-block-size as 10MB
TEST $CLI volume set $V0 stripe-block-size 10MB
EXPECT '10MB' volinfo_field $V0 'cluster.stripe-block-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a distributed stripe replicate volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 stripe 2 replica 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT 'Distributed-Striped-Replicate' volinfo_field $V0 'Type';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Setting stripe-block-size as 10MB
TEST $CLI volume set $V0 stripe-block-size 10MB
EXPECT '10MB' volinfo_field $V0 'cluster.stripe-block-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
