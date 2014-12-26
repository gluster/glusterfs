#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

cli1=$(echo $CLI | sed 's/ --wignore//')

# Creating volume with non resolvable host name
TEST ! $cli1 volume create $V0 replica 2 $H0:$B0/${V0}0 redhat:$B0/${V0}1 \
                                        $H0:$B0/${V0}2 redhat:$B0/${V0}3

# Creating distribute-replica volume with bad brick order. It will fail
# due to bad brick order.
TEST ! $cli1 volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
                                        $H0:$B0/${V0}2 $H0:$B0/${V0}3

# Now with force at the end of command it will bypass brick-order check
# for replicate or distribute-replicate volume. and it will create volume
TEST $cli1 volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
                                      $H0:$B0/${V0}2 $H0:$B0/${V0}3 force

cleanup;
