#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1}

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';


## Make sure automatic self-heal doesn't perturb our results.
TEST $CLI volume set $V0 cluster.self-heal-daemon off

TEST $CLI volume set $V0 background-self-heal-count 0

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

TEST `echo "TEST-FILE" > $M0/File`
TEST `mkdir $M0/Dir`
TEST kill_brick $V0 $H0 $B0/${V0}-0

TEST `ln -s $M0/File $M0/Link1`
TEST `ln -s $M0/Dir $M0/Link2`

TEST $CLI volume start $V0 force

TEST `find $M0/ | xargs stat 2>/dev/null 1>/dev/null`

sleep 60

TEST stat $B0/${V0}-0/Link1
TEST stat $B0/${V0}-0/Link2

cleanup;
