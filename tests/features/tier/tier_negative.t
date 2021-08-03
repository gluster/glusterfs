#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

cold_dir=$(mktemp -d -u -p ${PWD})
#TEST mkdir -p ${cold_dir};
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;

TEST ! $CLI volume start $V0;


#TEST rm -rf $cold_dir;

cleanup;
