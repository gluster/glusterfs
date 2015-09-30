#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0}
TEST $CLI volume set $V0 diagnostics.brick-log-buf-size 0
TEST ! $CLI volume set $V0 diagnostics.brick-log-buf-size -0
cleanup
