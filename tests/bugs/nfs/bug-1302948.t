#!/bin/bash
# TEST the nfs.rdirplus option
. $(dirname $0)/../../include.rc

cleanup
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
TEST $CLI volume set $V0 nfs.rdirplus off
TEST $CLI volume set $V0 nfs.rdirplus on
cleanup
