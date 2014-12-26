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

## Setting cluster.subvols-per-directory as -5
TEST ! $CLI volume set $V0 cluster.subvols-per-directory -5
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';
TEST ! $CLI volume set $V0 subvols-per-directory -5
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';

## Setting cluster.subvols-per-directory as 0
TEST ! $CLI volume set $V0 cluster.subvols-per-directory 0
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';
TEST ! $CLI volume set $V0 subvols-per-directory 0
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';

## Setting cluster.subvols-per-directory as 4 (the total number of bricks)
TEST ! $CLI volume set $V0 cluster.subvols-per-directory 4
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';
TEST ! $CLI volume set $V0 subvols-per-directory 4
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';

## Setting cluster.subvols-per-directory as 2 (the total number of subvolumes)
TEST $CLI volume set $V0 cluster.subvols-per-directory 2
EXPECT '2' volinfo_field $V0 'cluster.subvols-per-directory';

## Setting cluster.subvols-per-directory as 1
TEST $CLI volume set $V0 subvols-per-directory 1
EXPECT '1' volinfo_field $V0 'cluster.subvols-per-directory';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a pure replicate volume
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

## Setting cluster.subvols-per-directory as 8 for a replicate volume
TEST ! $CLI volume set $V0 cluster.subvols-per-directory 8
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';
TEST ! $CLI volume set $V0 subvols-per-directory 8
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';

## Setting cluster.subvols-per-directory as 1 for a replicate volume
TEST $CLI volume set $V0 cluster.subvols-per-directory 1
EXPECT '1' volinfo_field $V0 'cluster.subvols-per-directory';
TEST $CLI volume set $V0 subvols-per-directory 1
EXPECT '1' volinfo_field $V0 'cluster.subvols-per-directory';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;

## Start and create a pure stripe volume
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

## Setting cluster.subvols-per-directory as 8 for a stripe volume
TEST ! $CLI volume set $V0 cluster.subvols-per-directory 8
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';
TEST ! $CLI volume set $V0 subvols-per-directory 8
EXPECT '' volinfo_field $V0 'cluster.subvols-per-directory';

## Setting cluster.subvols-per-directory as 1 for a stripe volume
TEST $CLI volume set $V0 cluster.subvols-per-directory 1
EXPECT '1' volinfo_field $V0 'cluster.subvols-per-directory';
TEST $CLI volume set $V0 subvols-per-directory 1
EXPECT '1' volinfo_field $V0 'cluster.subvols-per-directory';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
