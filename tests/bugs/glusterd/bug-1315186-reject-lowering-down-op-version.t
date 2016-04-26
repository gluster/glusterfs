#! /bin/bash

. $(dirname $0)/../../include.rc

# The test validates that lowering down the op-version should fail

cleanup

TEST glusterd
TEST pidof glusterd

#volume create is just to ensure glusterd.info file is created
TEST $CLI volume create $V0 $H0:$B0/b1

GDWD=$($CLI system getwd)
OP_VERS_ORIG=$(grep 'operating-version' ${GDWD}/glusterd.info | cut -d '=' -f 2)
OP_VERS_NEW=`expr $OP_VERS_ORIG-1`

TEST ! $CLI volume set all $V0 cluster.op-version $OP_VERS_NEW

cleanup;

