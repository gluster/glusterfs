#!/bin/bash

# Test that "system getspec" works without op_version problems.

. $(dirname $0)/../../include.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info
TEST $CLI volume create $V0 $H0:$B0/brick1
TEST $CLI system getspec $V0
cleanup;
