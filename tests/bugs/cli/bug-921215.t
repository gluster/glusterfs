#!/bin/bash

# This is test case for bug no 921215 "Can not create volume with a . in the name"

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST ! $CLI volume create $V0.temp replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1

cleanup;
