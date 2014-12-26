#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2;
TEST $CLI volume start $V0;

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

TEST $CLI volume set $V0 changelog on
TEST $CLI volume set $V0 changelog.fsync-interval 1

# perform I/O on the background
f=$(basename `mktemp -t ${0##*/}.XXXXXX`)
dd if=/dev/urandom of=$M0/$f count=100000 bs=4k &

# this is the best we can do without inducing _error points_ in the code
# without the patch reconfigre() would hang...
TEST $CLI volume set $V0 changelog.rollover-time `expr $((RANDOM % 9)) + 1`
TEST $CLI volume set $V0 changelog.rollover-time `expr $((RANDOM % 9)) + 1`

TEST $CLI volume set $V0 changelog off
TEST $CLI volume set $V0 changelog on
TEST $CLI volume set $V0 changelog off
TEST $CLI volume set $V0 changelog on

TEST $CLI volume set $V0 changelog.rollover-time `expr $((RANDOM % 9)) + 1`
TEST $CLI volume set $V0 changelog.rollover-time `expr $((RANDOM % 9)) + 1`

# if there's a deadlock, this would hang
wait;

cleanup;
