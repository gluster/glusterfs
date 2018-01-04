#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

#bug-1314649 - validate group virt
TEST $CLI volume set $V0 group virt;

#bug-765230 - remove-quota-related-option-after-disabling-quota
## setting soft-timeout as 20
TEST $CLI volume set $V0 features.soft-timeout 20
EXPECT '20' volinfo_field $V0 'features.soft-timeout';

## enabling features.quota-deem-statfs
TEST ! $CLI volume set $V0 features.quota-deem-statfs on
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

## enabling quota
TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'

## eetting soft-timeout as 20
TEST $CLI volume set $V0 features.soft-timeout 20
EXPECT '20' volinfo_field $V0 'features.soft-timeout';

## enabling features.quota-deem-statfs
TEST $CLI volume set $V0 features.quota-deem-statfs on
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

## disabling quota
TEST $CLI volume quota $V0 disable
EXPECT 'off' volinfo_field $V0 'features.quota'
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'
EXPECT '' volinfo_field $V0 'features.soft-timeout'

## setting soft-timeout as 30
TEST $CLI volume set $V0 features.soft-timeout 30
EXPECT '30' volinfo_field $V0 'features.soft-timeout';

## disabling features.quota-deem-statfs
TEST ! $CLI volume set $V0 features.quota-deem-statfs off
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

#bug-859927 - validate different options for striped replicated volume

TEST ! $CLI volume set $V0 statedump-path ""
TEST ! $CLI volume set $V0 statedump-path "     "
TEST   $CLI volume set $V0 statedump-path "/home/"
EXPECT "/home/" volume_option $V0 server.statedump-path

TEST ! $CLI volume set $V0 background-self-heal-count ""
TEST ! $CLI volume set $V0 background-self-heal-count "      "
TEST   $CLI volume set $V0 background-self-heal-count 10
EXPECT "10" volume_option $V0 cluster.background-self-heal-count

TEST ! $CLI volume set $V0 cache-size ""
TEST ! $CLI volume set $V0 cache-size "    "
TEST   $CLI volume set $V0 cache-size 512MB
EXPECT "512MB" volume_option $V0 performance.cache-size

TEST ! $CLI volume set $V0 self-heal-daemon ""
TEST ! $CLI volume set $V0 self-heal-daemon "    "
TEST   $CLI volume set $V0 self-heal-daemon on
EXPECT "on" volume_option $V0 cluster.self-heal-daemon

TEST ! $CLI volume set $V0 read-subvolume ""
TEST ! $CLI volume set $V0 read-subvolume "    "
TEST   $CLI volume set $V0 read-subvolume $V0-client-0
EXPECT "$V0-client-0" volume_option  $V0 cluster.read-subvolume

TEST ! $CLI volume set $V0 data-self-heal-algorithm ""
TEST ! $CLI volume set $V0 data-self-heal-algorithm "     "
TEST ! $CLI volume set $V0 data-self-heal-algorithm on
TEST   $CLI volume set $V0 data-self-heal-algorithm full
EXPECT "full" volume_option $V0 cluster.data-self-heal-algorithm

TEST ! $CLI volume set $V0 min-free-inodes ""
TEST ! $CLI volume set $V0 min-free-inodes "     "
TEST  $CLI volume set $V0 min-free-inodes 60%
EXPECT "60%" volume_option $V0 cluster.min-free-inodes

TEST ! $CLI volume set $V0 min-free-disk ""
TEST ! $CLI volume set $V0 min-free-disk "     "
TEST  $CLI volume set $V0 min-free-disk 60%
EXPECT "60%" volume_option $V0 cluster.min-free-disk

TEST  $CLI volume set $V0 min-free-disk 120
EXPECT "120" volume_option $V0 cluster.min-free-disk

TEST ! $CLI volume set $V0 frame-timeout ""
TEST ! $CLI volume set $V0 frame-timeout "      "
TEST  $CLI volume set $V0 frame-timeout 0
EXPECT "0" volume_option $V0 network.frame-timeout

TEST ! $CLI volume set $V0 auth.allow ""
TEST ! $CLI volume set $V0 auth.allow "       "
TEST   $CLI volume set $V0 auth.allow 192.168.122.1
EXPECT "192.168.122.1" volume_option $V0 auth.allow

TEST ! $CLI volume set $V0 stripe-block-size ""
TEST ! $CLI volume set $V0 stripe-block-size "      "
TEST  $CLI volume set $V0 stripe-block-size 512MB
EXPECT "512MB" volume_option $V0 cluster.stripe-block-size

#bug-782095 - validate performance cache min/max size value

## setting performance cache min size as 2MB
TEST $CLI volume set $V0 performance.cache-min-file-size 2MB
EXPECT '2MB' volinfo_field $V0 'performance.cache-min-file-size';

## setting performance cache max size as 20MB
TEST $CLI volume set $V0 performance.cache-max-file-size 20MB
EXPECT '20MB' volinfo_field $V0 'performance.cache-max-file-size';

## trying to set performance cache min size as 25MB
TEST ! $CLI volume set $V0 performance.cache-min-file-size 25MB
EXPECT '2MB' volinfo_field $V0 'performance.cache-min-file-size';

## able to set performance cache min size as long as its lesser than max size
TEST $CLI volume set $V0 performance.cache-min-file-size 15MB
EXPECT '15MB' volinfo_field $V0 'performance.cache-min-file-size';

## trying it out with only cache-max-file-size in CLI as 10MB
TEST ! $CLI volume set $V0 cache-max-file-size 10MB
EXPECT '20MB' volinfo_field $V0 'performance.cache-max-file-size';

## finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup
