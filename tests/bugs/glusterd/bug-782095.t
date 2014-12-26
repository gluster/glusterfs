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

## Setting performance cache min size as 2MB
TEST $CLI volume set $V0 performance.cache-min-file-size 2MB
EXPECT '2MB' volinfo_field $V0 'performance.cache-min-file-size';

## Setting performance cache max size as 20MB
TEST $CLI volume set $V0 performance.cache-max-file-size 20MB
EXPECT '20MB' volinfo_field $V0 'performance.cache-max-file-size';

## Trying to set performance cache min size as 25MB
TEST ! $CLI volume set $V0 performance.cache-min-file-size 25MB
EXPECT '2MB' volinfo_field $V0 'performance.cache-min-file-size';

## Able to set performance cache min size as long as its lesser than max size
TEST $CLI volume set $V0 performance.cache-min-file-size 15MB
EXPECT '15MB' volinfo_field $V0 'performance.cache-min-file-size';

## Trying it out with only cache-max-file-size in CLI as 10MB
TEST ! $CLI volume set $V0 cache-max-file-size 10MB
EXPECT '20MB' volinfo_field $V0 'performance.cache-max-file-size';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
