#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI system uuid reset;

uuid1=$(grep UUID $GLUSTERD_WORKDIR/glusterd.info | cut -f 2 -d "=");

TEST $CLI system uuid reset;
uuid2=$(grep UUID $GLUSTERD_WORKDIR/glusterd.info | cut -f 2 -d "=");

TEST [ $uuid1 != $uuid2 ]

cleanup
