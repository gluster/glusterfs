#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST "echo volume list | $CLI --xml | xmllint --format -"

cleanup
