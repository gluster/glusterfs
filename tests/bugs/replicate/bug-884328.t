#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

TEST check_option_help_presence "cluster.quorum-type"
TEST check_option_help_presence "cluster.quorum-count"
cleanup;
