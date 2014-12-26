#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1

TEST $CLI volume set $V0 features.uss disable;

TEST killall glusterd;

rm -f $GLUSTERD_WORKDIR/vols/$V0/snapd.info

TEST glusterd

cleanup  ;
