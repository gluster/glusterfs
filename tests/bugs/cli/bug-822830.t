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

## Setting nfs.rpc-auth-reject as 192.{}.1.2
TEST ! $CLI volume set $V0 nfs.rpc-auth-reject 192.{}.1.2
EXPECT '' volinfo_field $V0 'nfs.rpc-auth-reject';

# Setting nfs.rpc-auth-allow as a.a.
TEST ! $CLI volume set $V0 nfs.rpc-auth-allow a.a.
EXPECT '' volinfo_field $V0 'nfs.rpc-auth-allow';

## Setting nfs.rpc-auth-reject as 192.*..*
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.*..*
EXPECT '192.*..*' volinfo_field $V0 'nfs.rpc-auth-reject';

# Setting nfs.rpc-auth-allow as a.a
TEST $CLI volume set $V0 nfs.rpc-auth-allow a.a
EXPECT 'a.a' volinfo_field $V0 'nfs.rpc-auth-allow';

# Setting nfs.rpc-auth-allow as *.redhat.com
TEST $CLI volume set $V0 nfs.rpc-auth-allow *.redhat.com
EXPECT '\*.redhat.com' volinfo_field $V0 'nfs.rpc-auth-allow';

# Setting nfs.rpc-auth-allow as 192.168.10.[1-5]
TEST $CLI volume set $V0 nfs.rpc-auth-allow 192.168.10.[1-5]
EXPECT '192.168.10.\[1-5]' volinfo_field $V0 'nfs.rpc-auth-allow';

# Setting nfs.rpc-auth-allow as 192.168.70.?
TEST $CLI volume set $V0 nfs.rpc-auth-allow 192.168.70.?
EXPECT '192.168.70.?' volinfo_field $V0 'nfs.rpc-auth-allow';

# Setting nfs.rpc-auth-reject as 192.168.10.5/16
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.168.10.5/16
EXPECT '192.168.10.5/16' volinfo_field $V0 'nfs.rpc-auth-reject';

## Setting nfs.rpc-auth-reject as 192.*.*
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.*.*
EXPECT '192.*.*' volinfo_field $V0 'nfs.rpc-auth-reject';

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
