#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

glusterd;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

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
TEST   $CLI volume set $V0 min-free-inodes 60%
EXPECT "60%" volume_option $V0 cluster.min-free-inodes

TEST ! $CLI volume set $V0 min-free-disk ""
TEST ! $CLI volume set $V0 min-free-disk "     "
TEST   $CLI volume set $V0 min-free-disk 60%
EXPECT "60%" volume_option $V0 cluster.min-free-disk

TEST   $CLI volume set $V0 min-free-disk 120
EXPECT "120" volume_option $V0 cluster.min-free-disk

TEST ! $CLI volume set $V0 frame-timeout ""
TEST ! $CLI volume set $V0 frame-timeout "      "
TEST   $CLI volume set $V0 frame-timeout 0
EXPECT "0" volume_option $V0 network.frame-timeout

TEST ! $CLI volume set $V0 auth.allow ""
TEST ! $CLI volume set $V0 auth.allow "       "
TEST   $CLI volume set $V0 auth.allow 192.168.122.1
EXPECT "192.168.122.1" volume_option $V0 auth.allow

TEST ! $CLI volume set $V0 stripe-block-size ""
TEST ! $CLI volume set $V0 stripe-block-size "      "
TEST   $CLI volume set $V0 stripe-block-size 512MB
EXPECT "512MB" volume_option $V0 cluster.stripe-block-size

cleanup;
