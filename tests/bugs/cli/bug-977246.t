#! /bin/bash

# This test checks if address validation, correctly catches hostnames
# with consective dots, such as 'example..org', as invalid

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume info $V0
TEST $CLI volume start $V0

TEST ! $CLI volume set $V0 auth.allow example..org

TEST $CLI volume stop $V0

cleanup;
