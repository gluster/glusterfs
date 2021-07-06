#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Start glusterd
TEST glusterd
TEST pidof glusterd

# Create and start a volume
TEST $CLI volume create $V0 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
TEST $CLI volume start $V0

# Set an option which doesn't satisfy the dependency chain
TEST ! $CLI volume set $V0 performance.parallel-readdir on

# Set an option which satisfy the dependency chain
TEST $CLI volume set $V0 performance.readdir-ahead on

# Set multiple options which satisfy the dependency chain
TEST ! $CLI volume set $V0 performance.parallel-readdir on performance.readdir-ahead off

# Disable readdir-ahead, set above
TEST $CLI volume set $V0 performance.readdir-ahead off

# Set multiple options which satisfy the dependency chain
TEST $CLI volume set $V0 performance.parallel-readdir on performance.readdir-ahead on

# Disable the options
TEST $CLI volume set $V0 performance.parallel-readdir off
TEST $CLI volume set $V0 performance.force-readdirp off dht.force-readdirp off

# Set multiple options which satisfy the dependency chain
TEST $CLI volume set $V0 performance.parallel-readdir on performance.force-readdirp on

# Setting some other option
TEST $CLI volume set $V0 features.ctime off
TEST $CLI volume set all cluster.localtime-logging enable
TEST $CLI volume set all cluster.brick-multiplex enable


cleanup;