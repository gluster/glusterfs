#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd

# Create and start the volume
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

logdir=`gluster --print-logdir`

# Switch off performance translators
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.readdir-ahead off

# Enable optimal mandatory-locking mode and restart the volume
TEST $CLI volume set $V0 locks.mandatory-locking optimal
TEST $CLI volume stop $V0
TEST $CLI volume start $V0

# Compile and run the test program
TEST build_tester $(dirname $0)/mandatory-lock-optimal.c -lgfapi
TEST ./$(dirname $0)/mandatory-lock-optimal $H0 $V0 $logdir

# Cleanup the environment
cleanup_tester $(dirname $0)/mandatory-lock-optimal
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
