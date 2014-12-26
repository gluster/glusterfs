#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST ! $CLI volume set $V0 performance.open-behind off;

TEST pidof glusterd;

cleanup;
