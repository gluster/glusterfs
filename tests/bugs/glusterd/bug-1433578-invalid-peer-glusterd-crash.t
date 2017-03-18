#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

TEST ! $CLI peer probe invalid-peer

TEST pidof glusterd;
cleanup;
