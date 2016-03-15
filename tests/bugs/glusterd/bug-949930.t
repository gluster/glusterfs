#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

V1=patchy2

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume start $V0;

TEST $CLI volume create $V1 $H0:$B0/${V1}{1,2};
TEST $CLI volume set $V1 nfs.disable off
TEST $CLI volume start $V1;

TEST ! $CLI volume set $V0 performance.nfs.read-ahead blah
EXPECT '' volume_option $V0 performance.nfs.read-ahead

TEST $CLI volume set $V0 performance.nfs.read-ahead on
EXPECT "on" volume_option $V0 performance.nfs.read-ahead

EXPECT '' volume_option $V1 performance.nfs.read-ahead

cleanup;

