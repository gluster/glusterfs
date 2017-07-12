#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0};
TEST $CLI volume start $V0;
logdir=`gluster --print-logdir`


TEST build_tester $(dirname $0)/disallow-gfid-volumeid-fremovexattr.c -lgfapi
TEST $(dirname $0)/disallow-gfid-volumeid-fremovexattr $H0 $V0 $logdir/disallow-gfid-volumeid-fremovexattr.log

cleanup_tester $(dirname $0)/disallow-gfid-volumeid-fremovexattr
cleanup;
