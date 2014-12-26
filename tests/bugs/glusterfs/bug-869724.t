#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;


## Start and create a volume
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1;

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';


## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';


## Make volume tightly consistent for metdata
TEST $CLI volume set $V0 performance.stat-prefetch off;

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

touch $M0/test;
build_tester $(dirname $0)/getlk_owner.c

TEST $(dirname $0)/getlk_owner $M0/test;

rm -f $(dirname $0)/getlk_owner
cleanup;

