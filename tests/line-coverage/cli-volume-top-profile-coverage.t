#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../cluster.rc
. $(dirname $0)/../volume.rc

cleanup

# Creating cluster
TEST launch_cluster 3

# Probing peers
TEST $CLI_1 peer probe $H2
TEST $CLI_1 peer probe $H3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 3

# Creating a volume and starting it.
TEST $CLI_1 volume create $V0 replica 3 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0
TEST $CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

TEST glusterfs -s $H1 --volfile-id $V0 $M1
TEST touch $M1/file{1..100}

# Testing volume top command with and without xml output
function test_volume_top_cmds () {
    local ret=0
    declare -a top_cmds=("read" "open" "write" "opendir" "readdir")
    for cmd in ${top_cmds[@]}; do
        $CLI_1 volume top $V0 $cmd
        (( ret += $? ))
        $CLI_1 volume top $V0 clear
        (( ret += $? ))
        $CLI_1 volume top $V0 $cmd --xml
        (( ret += $? ))
        $CLI_1 volume top $V0 $cmd brick $H1:$B1/$V0
        (( ret += $? ))
        $CLI_1 volume top $V0 clear brick $H1:$B1/$V0
        (( ret += $? ))
        $CLI_1 volume top $V0 $cmd brick $H1:$B1/$V0 --xml
        (( ret += $? ))
    done
    return $ret
}

# Testing volume profile command with and without xml
function test_volume_profile_cmds () {
    local ret=0
    declare -a profile_cmds=("start" "info" "info peek" "info cumulative" "info clear" "info incremental peek" "stop")
    for cmd in "${profile_cmds[@]}"; do
        $CLI_1 volume profile $V0 $cmd
        (( ret += $? ))
        $CLI_1 volume profile $V0 $cmd --xml
        (( ret += $? ))
    done
    return $ret
}

TEST test_volume_top_cmds;
TEST test_volume_profile_cmds;

cleanup
