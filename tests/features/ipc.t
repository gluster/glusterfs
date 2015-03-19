#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;
mkdir -p $B0/1
mkdir -p $M0

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/1
TEST $CLI volume start $V0

# Find OS-dependent EOPNOTSUPP value from system headers
EOPNOTSUPP=$( echo '#include <errno.h>\\EOPNOTSUPP\\' | tr '\\' '\n' | \
              cc -E -c - | tail -1 )

# liglusterfs embbeds its own UUID implementation. The function name
# may be the same as in built(in implementation from libc, but with
# different prototype. In that case, we must make sure python will
# use libglusterfs's version, and dlopen() does not make any guarantee
# on this. By preloading libglusterfs.so before launching python, we
# ensure libglusterfs's UUID functions will be used.
LD_PRELOAD=${prefix}/lib/libglusterfs.so
export LD_PRELOAD

# This is a pretty lame test.  Basically we just want to make sure that we
# get all the way through the translator stacks on client and server to get a
# simple error (EOPNOTSUPP) instead of a crash, RPC error, etc.
EXPECT ${EOPNOTSUPP}  $PYTHON $(dirname $0)/ipctest.py $H0 $V0

unset LD_PRELOAD
cleanup;
