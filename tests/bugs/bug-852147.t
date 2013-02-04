#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id=$V0 $M0
touch $M0/file1;

TEST $CLI volume set $V0 performance.cache-max-file-size 20MB
TEST $CLI volume set $V0 performance.cache-min-file-size 10MB

EXPECT "20MB" volinfo_field $V0 'performance.cache-max-file-size';
EXPECT "10MB" volinfo_field $V0 'performance.cache-min-file-size';

TEST $CLI volume reset $V0
EXPECT "" volinfo_field $V0 'performance.cache-max-file-size';
EXPECT "" volinfo_field $V0 'performance.cache-min-file-size';

EXPECT "Starting volume profile on $V0 has been successful " $CLI volume profile $V0 start

function vol_prof_info()
{
    $CLI volume profile $V0 info | grep Brick | wc -l
}
EXPECT "8" vol_prof_info

EXPECT "Stopping volume profile on $V0 has been successful " $CLI volume profile $V0 stop

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
