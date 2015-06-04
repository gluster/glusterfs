#!/bin/bash

# This regression test tries to ensure that quota limit-usage set work with
# features.cache-invalidation on.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4,5,6};
TEST $CLI volume start $V0;

TEST $CLI volume set $V0 features.cache-invalidation on;
TEST $CLI volume quota $V0 enable;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST mkdir -p $M0/1/2;
TEST $CLI volume quota $V0 limit-usage /1/2 100MB 70%;

TEST $CLI volume status $V0
TEST $CLI volume stop $V0

cleanup;
