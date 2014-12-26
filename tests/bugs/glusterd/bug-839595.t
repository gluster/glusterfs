#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 cluster.server-quorum-type server
EXPECT "server" volume_option $V0 cluster.server-quorum-type
TEST $CLI volume set $V0 cluster.server-quorum-type none
EXPECT "none" volume_option $V0 cluster.server-quorum-type
TEST $CLI volume reset $V0 cluster.server-quorum-type
TEST ! $CLI volume set $V0 cluster.server-quorum-type abc
TEST ! $CLI volume set all cluster.server-quorum-type none
TEST ! $CLI volume set $V0 cluster.server-quorum-ratio 100

TEST ! $CLI volume set all cluster.server-quorum-ratio abc
TEST ! $CLI volume set all cluster.server-quorum-ratio -1
TEST ! $CLI volume set all cluster.server-quorum-ratio 100.0000005
TEST $CLI volume set all cluster.server-quorum-ratio 0
EXPECT "0" volume_option $V0 cluster.server-quorum-ratio
TEST $CLI volume set all cluster.server-quorum-ratio 100
EXPECT "100" volume_option $V0 cluster.server-quorum-ratio
TEST $CLI volume set all cluster.server-quorum-ratio 0.0000005
EXPECT "0.0000005" volume_option $V0 cluster.server-quorum-ratio
TEST $CLI volume set all cluster.server-quorum-ratio 100%
EXPECT "100%" volume_option $V0 cluster.server-quorum-ratio
cleanup;
