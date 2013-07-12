#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '8' brick_count $V0

TEST $CLI volume set $V0 nufa on;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE with caching disabled (read-only)
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 --read-only -s $H0 --volfile-id $V0 $M1;

## Wait for volume to register with rpc.mountd
sleep 5;

## Mount NFS
TEST mount -t nfs -o nolock,soft,intr $H0:/$V0 $N0;

cleanup;
