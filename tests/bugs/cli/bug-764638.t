#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI pool list;
TEST $CLI pool list --xml;

cleanup;
